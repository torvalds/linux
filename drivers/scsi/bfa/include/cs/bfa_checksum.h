/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

/**
 *  bfa_checksum.h BFA checksum utilities
 */

#ifndef __BFA_CHECKSUM_H__
#define __BFA_CHECKSUM_H__

static inline u32
bfa_checksum_u32(u32 *buf, int sz)
{
	int		i, m = sz >> 2;
	u32	sum = 0;

	for (i = 0; i < m; i++)
		sum ^= buf[i];

	return (sum);
}

static inline u16
bfa_checksum_u16(u16 *buf, int sz)
{
	int             i, m = sz >> 1;
	u16        sum = 0;

	for (i = 0; i < m; i++)
		sum ^= buf[i];

	return (sum);
}

static inline u8
bfa_checksum_u8(u8 *buf, int sz)
{
	int             i;
	u8         sum = 0;

	for (i = 0; i < sz; i++)
		sum ^= buf[i];

	return (sum);
}
#endif

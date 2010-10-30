/*
 * Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
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
 * Contains declarations all OS Specific files needed for BFA layer
 */

#ifndef __BFA_OS_INC_H__
#define __BFA_OS_INC_H__

#include <linux/types.h>
#include <linux/version.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/scsi_transport.h>

#ifdef __BIG_ENDIAN
#define __BIGENDIAN
#endif

static inline u64 bfa_os_get_clock(void)
{
	return jiffies;
}

static inline u64 bfa_os_get_log_time(void)
{
	u64 system_time = 0;
	struct timeval tv;
	do_gettimeofday(&tv);

	/* We are interested in seconds only. */
	system_time = tv.tv_sec;
	return system_time;
}

#define bfa_io_lat_clock_res_div HZ
#define bfa_io_lat_clock_res_mul 1000

#define BFA_ASSERT(p) do {						\
	if (!(p)) {      \
		printk(KERN_ERR "assert(%s) failed at %s:%d\n",		\
		#p, __FILE__, __LINE__);      \
	}								\
} while (0)

#define BFA_LOG(level, bfad, mask, fmt, arg...)				\
do {									\
	if (((mask) == 4) || (level[1] <= '4'))				\
		dev_printk(level, &((bfad)->pcidev)->dev, fmt, ##arg);	\
} while (0)

#define bfa_swap_3b(_x)				\
	((((_x) & 0xff) << 16) |		\
	((_x) & 0x00ff00) |			\
	(((_x) & 0xff0000) >> 16))

#define bfa_swap_8b(_x)					\
	((((_x) & 0xff00000000000000ull) >> 56)		\
	 | (((_x) & 0x00ff000000000000ull) >> 40)	\
	 | (((_x) & 0x0000ff0000000000ull) >> 24)	\
	 | (((_x) & 0x000000ff00000000ull) >> 8)	\
	 | (((_x) & 0x00000000ff000000ull) << 8)	\
	 | (((_x) & 0x0000000000ff0000ull) << 24)	\
	 | (((_x) & 0x000000000000ff00ull) << 40)	\
	 | (((_x) & 0x00000000000000ffull) << 56))

#define bfa_os_swap32(_x)			\
	((((_x) & 0xff) << 24)		|	\
	(((_x) & 0x0000ff00) << 8)	|	\
	(((_x) & 0x00ff0000) >> 8)	|	\
	(((_x) & 0xff000000) >> 24))

#define bfa_os_swap_sgaddr(_x)  ((u64)(                                 \
	(((u64)(_x) & (u64)0x00000000000000ffull) << 32)        |       \
	(((u64)(_x) & (u64)0x000000000000ff00ull) << 32)        |       \
	(((u64)(_x) & (u64)0x0000000000ff0000ull) << 32)        |       \
	(((u64)(_x) & (u64)0x00000000ff000000ull) << 32)        |       \
	(((u64)(_x) & (u64)0x000000ff00000000ull) >> 32)        |       \
	(((u64)(_x) & (u64)0x0000ff0000000000ull) >> 32)        |       \
	(((u64)(_x) & (u64)0x00ff000000000000ull) >> 32)        |       \
	(((u64)(_x) & (u64)0xff00000000000000ull) >> 32)))

#ifndef __BIGENDIAN
#define bfa_os_htons(_x) ((u16)((((_x) & 0xff00) >> 8) | \
				 (((_x) & 0x00ff) << 8)))
#define bfa_os_htonl(_x)	bfa_os_swap32(_x)
#define bfa_os_htonll(_x)	bfa_swap_8b(_x)
#define bfa_os_hton3b(_x)	bfa_swap_3b(_x)
#define bfa_os_wtole(_x)   (_x)
#define bfa_os_sgaddr(_x)  (_x)

#else

#define bfa_os_htons(_x)   (_x)
#define bfa_os_htonl(_x)   (_x)
#define bfa_os_hton3b(_x)  (_x)
#define bfa_os_htonll(_x)  (_x)
#define bfa_os_wtole(_x)   bfa_os_swap32(_x)
#define bfa_os_sgaddr(_x)  bfa_os_swap_sgaddr(_x)

#endif

#define bfa_os_ntohs(_x)   bfa_os_htons(_x)
#define bfa_os_ntohl(_x)   bfa_os_htonl(_x)
#define bfa_os_ntohll(_x)  bfa_os_htonll(_x)
#define bfa_os_ntoh3b(_x)  bfa_os_hton3b(_x)

#define bfa_os_u32(__pa64) ((__pa64) >> 32)

#define bfa_os_memset	memset
#define bfa_os_memcpy	memcpy
#define bfa_os_udelay	udelay
#define bfa_os_vsprintf vsprintf
#define bfa_os_snprintf snprintf

#define bfa_os_assign(__t, __s) __t = __s
#define bfa_os_addr_t void __iomem *

#define bfa_os_reg_read(_raddr) readl(_raddr)
#define bfa_os_reg_write(_raddr, _val) writel((_val), (_raddr))
#define bfa_os_mem_read(_raddr, _off)					\
	bfa_os_swap32(readl(((_raddr) + (_off))))
#define bfa_os_mem_write(_raddr, _off, _val)				\
	writel(bfa_os_swap32((_val)), ((_raddr) + (_off)))

#define BFA_TRC_TS(_trcm)						\
			({						\
				struct timeval tv;			\
									\
				do_gettimeofday(&tv);      \
				(tv.tv_sec*1000000+tv.tv_usec);      \
			 })

#define boolean_t int

/**
 * For current time stamp, OS API will fill-in
 */
struct bfa_timeval_s {
	u32	tv_sec;		/*  seconds        */
	u32	tv_usec;	/*  microseconds   */
};

static inline void
bfa_os_gettimeofday(struct bfa_timeval_s *tv)
{
	struct timeval  tmp_tv;

	do_gettimeofday(&tmp_tv);
	tv->tv_sec = (u32) tmp_tv.tv_sec;
	tv->tv_usec = (u32) tmp_tv.tv_usec;
}

static inline void
wwn2str(char *wwn_str, u64 wwn)
{
	union {
		u64 wwn;
		u8 byte[8];
	} w;

	w.wwn = wwn;
	sprintf(wwn_str, "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x", w.byte[0],
		w.byte[1], w.byte[2], w.byte[3], w.byte[4], w.byte[5],
		w.byte[6], w.byte[7]);
}

static inline void
fcid2str(char *fcid_str, u32 fcid)
{
	union {
		u32 fcid;
		u8 byte[4];
	} f;

	f.fcid = fcid;
	sprintf(fcid_str, "%02x:%02x:%02x", f.byte[1], f.byte[2], f.byte[3]);
}

#endif /* __BFA_OS_INC_H__ */

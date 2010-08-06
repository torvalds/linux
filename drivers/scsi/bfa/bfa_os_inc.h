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
 * Contains declarations all OS Specific files needed for BFA layer
 */

#ifndef __BFA_OS_INC_H__
#define __BFA_OS_INC_H__

#ifndef __KERNEL__
#include <stdint.h>
#else
#include <linux/types.h>

#include <linux/version.h>
#include <linux/pci.h>

#include <linux/dma-mapping.h>
#define SET_MODULE_VERSION(VER)

#include <linux/idr.h>

#include <linux/interrupt.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>

#include <linux/workqueue.h>

#include <scsi/scsi.h>
#include <scsi/scsi_host.h>

#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/scsi_transport.h>

#ifdef __BIG_ENDIAN
#define __BIGENDIAN
#endif

#define BFA_ERR			KERN_ERR
#define BFA_WARNING		KERN_WARNING
#define BFA_NOTICE		KERN_NOTICE
#define BFA_INFO		KERN_INFO
#define BFA_DEBUG		KERN_DEBUG

#define LOG_BFAD_INIT		0x00000001
#define LOG_FCP_IO		0x00000002

#ifdef DEBUG
#define BFA_LOG_TRACE(bfad, level, mask, fmt, arg...)			\
		BFA_LOG(bfad, level, mask, fmt, ## arg)
#define BFA_DEV_TRACE(bfad, level, fmt, arg...)				\
		BFA_DEV_PRINTF(bfad, level, fmt, ## arg)
#define BFA_TRACE(level, fmt, arg...)					\
		BFA_PRINTF(level, fmt, ## arg)
#else
#define BFA_LOG_TRACE(bfad, level, mask, fmt, arg...)
#define BFA_DEV_TRACE(bfad, level, fmt, arg...)
#define BFA_TRACE(level, fmt, arg...)
#endif

#define BFA_ASSERT(p) do {						\
	if (!(p)) {      \
		printk(KERN_ERR "assert(%s) failed at %s:%d\n",		\
		#p, __FILE__, __LINE__);      \
		BUG();      \
	}								\
} while (0)


#define BFA_LOG(bfad, level, mask, fmt, arg...)				\
do { 									\
	if (((mask) & (((struct bfad_s *)(bfad))->			\
		cfg_data[cfg_log_mask])) || (level[1] <= '3'))		\
		dev_printk(level, &(((struct bfad_s *)			\
			(bfad))->pcidev->dev), fmt, ##arg);      \
} while (0)

#ifndef BFA_DEV_PRINTF
#define BFA_DEV_PRINTF(bfad, level, fmt, arg...)			\
		dev_printk(level, &(((struct bfad_s *)			\
			(bfad))->pcidev->dev), fmt, ##arg);
#endif

#define BFA_PRINTF(level, fmt, arg...)					\
		printk(level fmt, ##arg);

int bfa_os_MWB(void *);

#define bfa_os_mmiowb()		mmiowb()

#define bfa_swap_3b(_x)				\
	((((_x) & 0xff) << 16) |		\
	((_x) & 0x00ff00) |			\
	(((_x) & 0xff0000) >> 16))

#define bfa_swap_8b(_x) 				\
     ((((_x) & 0xff00000000000000ull) >> 56)		\
      | (((_x) & 0x00ff000000000000ull) >> 40)		\
      | (((_x) & 0x0000ff0000000000ull) >> 24)		\
      | (((_x) & 0x000000ff00000000ull) >> 8)		\
      | (((_x) & 0x00000000ff000000ull) << 8)		\
      | (((_x) & 0x0000000000ff0000ull) << 24)		\
      | (((_x) & 0x000000000000ff00ull) << 40)		\
      | (((_x) & 0x00000000000000ffull) << 56))

#define bfa_os_swap32(_x) 			\
	((((_x) & 0xff) << 24) 		|	\
	(((_x) & 0x0000ff00) << 8)	|	\
	(((_x) & 0x00ff0000) >> 8)	|	\
	(((_x) & 0xff000000) >> 24))

#define bfa_os_swap_sgaddr(_x)	((u64)(					\
	(((u64)(_x) & (u64)0x00000000000000ffull) << 32)	|	\
	(((u64)(_x) & (u64)0x000000000000ff00ull) << 32)	|	\
	(((u64)(_x) & (u64)0x0000000000ff0000ull) << 32)	|	\
	(((u64)(_x) & (u64)0x00000000ff000000ull) << 32)	|	\
	(((u64)(_x) & (u64)0x000000ff00000000ull) >> 32)	|	\
	(((u64)(_x) & (u64)0x0000ff0000000000ull) >> 32)	|	\
	(((u64)(_x) & (u64)0x00ff000000000000ull) >> 32)	|	\
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

#define bfa_os_assign(__t, __s) __t = __s

#define bfa_os_addr_t char __iomem *
#define bfa_os_panic()

#define bfa_os_reg_read(_raddr) readl(_raddr)
#define bfa_os_reg_write(_raddr, _val) writel((_val), (_raddr))
#define bfa_os_mem_read(_raddr, _off)                                   \
	bfa_os_swap32(readl(((_raddr) + (_off))))
#define bfa_os_mem_write(_raddr, _off, _val)                            \
	writel(bfa_os_swap32((_val)), ((_raddr) + (_off)))

#define BFA_TRC_TS(_trcm)						\
			({						\
				struct timeval tv;			\
									\
				do_gettimeofday(&tv);      \
				(tv.tv_sec*1000000+tv.tv_usec);      \
			 })

struct bfa_log_mod_s;
void bfa_os_printf(struct bfa_log_mod_s *log_mod, u32 msg_id,
			const char *fmt, ...);
#endif

#define boolean_t int

/**
 * For current time stamp, OS API will fill-in
 */
struct bfa_timeval_s {
	u32	tv_sec;		/*  seconds        */
	u32	tv_usec;	/*  microseconds   */
};

void bfa_os_gettimeofday(struct bfa_timeval_s *tv);

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

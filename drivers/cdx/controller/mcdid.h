/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2008-2013 Solarflare Communications Inc.
 * Copyright (C) 2022-2025, Advanced Micro Devices, Inc.
 */

#ifndef CDX_MCDID_H
#define CDX_MCDID_H

#include <linux/mutex.h>
#include <linux/kref.h>
#include <linux/rpmsg.h>

#include "mc_cdx_pcol.h"

#ifdef DEBUG
#define CDX_WARN_ON_ONCE_PARANOID(x) WARN_ON_ONCE(x)
#define CDX_WARN_ON_PARANOID(x) WARN_ON(x)
#else
#define CDX_WARN_ON_ONCE_PARANOID(x) do {} while (0)
#define CDX_WARN_ON_PARANOID(x) do {} while (0)
#endif

#define MCDI_BUF_LEN (8 + MCDI_CTL_SDU_LEN_MAX)

static inline struct cdx_mcdi_iface *cdx_mcdi_if(struct cdx_mcdi *cdx)
{
	return cdx->mcdi ? &cdx->mcdi->iface : NULL;
}

int cdx_mcdi_rpc_async(struct cdx_mcdi *cdx, unsigned int cmd,
		       const struct cdx_dword *inbuf, size_t inlen,
		       cdx_mcdi_async_completer *complete,
		       unsigned long cookie);
int cdx_mcdi_wait_for_quiescence(struct cdx_mcdi *cdx,
				 unsigned int timeout_jiffies);

/*
 * We expect that 16- and 32-bit fields in MCDI requests and responses
 * are appropriately aligned, but 64-bit fields are only
 * 32-bit-aligned.
 */
#define MCDI_BYTE(_buf, _field)						\
	((void)BUILD_BUG_ON_ZERO(MC_CMD_ ## _field ## _LEN != 1),	\
	 *MCDI_PTR(_buf, _field))
#define MCDI_WORD(_buf, _field)						\
	((void)BUILD_BUG_ON_ZERO(MC_CMD_ ## _field ## _LEN != 2),	\
	 le16_to_cpu(*(__force const __le16 *)MCDI_PTR(_buf, _field)))
#define MCDI_POPULATE_DWORD_1(_buf, _field, _name1, _value1)		\
	CDX_POPULATE_DWORD_1(*_MCDI_DWORD(_buf, _field),		\
			     MC_CMD_ ## _name1, _value1)
#define MCDI_SET_QWORD(_buf, _field, _value)				\
	do {								\
		CDX_POPULATE_DWORD_1(_MCDI_DWORD(_buf, _field)[0],	\
				     CDX_DWORD, (u32)(_value));	\
		CDX_POPULATE_DWORD_1(_MCDI_DWORD(_buf, _field)[1],	\
				     CDX_DWORD, (u64)(_value) >> 32);	\
	} while (0)
#define MCDI_QWORD(_buf, _field)					\
	(CDX_DWORD_FIELD(_MCDI_DWORD(_buf, _field)[0], CDX_DWORD) |	\
	(u64)CDX_DWORD_FIELD(_MCDI_DWORD(_buf, _field)[1], CDX_DWORD) << 32)

#endif /* CDX_MCDID_H */

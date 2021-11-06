/* D11 macdbg functions for Broadcom 802.11abgn
 * Networking Adapter Device Drivers.
 *
 * Broadcom Proprietary and Confidential. Copyright (C) 2020,
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom;
 * the contents of this file may not be disclosed to third parties,
 * copied or duplicated in any form, in whole or in part, without
 * the prior written permission of Broadcom.
 *
 *
 * <<Broadcom-WL-IPTag/Proprietary:>>
 *
 * $Id: dhd_macdbg.c 670412 2016-11-15 20:01:18Z shinuk $
 */

#ifdef BCMDBG
#include <typedefs.h>
#include <osl.h>

#include <bcmutils.h>
#include <dhd_dbg.h>
#include <dhd_macdbg.h>
#include "d11reglist_proto.h"
#include "dhdioctl.h"
#include <sdiovar.h>

#ifdef BCMDBUS
#include <dbus.h>
#define BUS_IOVAR_OP(a, b, c, d, e, f, g) dbus_iovar_op(a->dbus, b, c, d, e, f, g)
#else
#include <dhd_bus.h>
#define BUS_IOVAR_OP dhd_bus_iovar_op
#endif

typedef struct _macdbg_info_t {
	dhd_pub_t *dhdp;
	d11regs_list_t *pd11regs;
	uint16 d11regs_sz;
	d11regs_list_t *pd11regs_x;
	uint16 d11regsx_sz;
	svmp_list_t *psvmpmems;
	uint16 svmpmems_sz;
} macdbg_info_t;

#define SVMPLIST_HARDCODE

int
dhd_macdbg_attach(dhd_pub_t *dhdp)
{
	macdbg_info_t *macdbg_info = MALLOCZ(dhdp->osh, sizeof(*macdbg_info));
#ifdef SVMPLIST_HARDCODE
	svmp_list_t svmpmems[] = {
		{0x20000, 256},
		{0x21e10, 16},
		{0x20300, 16},
		{0x20700, 16},
		{0x20b00, 16},
		{0x20be0, 16},
		{0x20bff, 16},
		{0xc000, 32},
		{0xe000, 32},
		{0x10000, 0x8000},
		{0x18000, 0x8000}
	};
#endif /* SVMPLIST_HARDCODE */

	if (macdbg_info == NULL) {
		return BCME_NOMEM;
	}
	dhdp->macdbg_info = macdbg_info;
	macdbg_info->dhdp = dhdp;

#ifdef SVMPLIST_HARDCODE
	macdbg_info->psvmpmems = MALLOCZ(dhdp->osh, sizeof(svmpmems));
	if (macdbg_info->psvmpmems == NULL) {
		return BCME_NOMEM;
	}

	macdbg_info->svmpmems_sz = ARRAYSIZE(svmpmems);
	memcpy(macdbg_info->psvmpmems, svmpmems, sizeof(svmpmems));

	DHD_ERROR(("%s: psvmpmems %p svmpmems_sz %d\n",
		__FUNCTION__, macdbg_info->psvmpmems, macdbg_info->svmpmems_sz));
#endif
	return BCME_OK;
}

void
dhd_macdbg_detach(dhd_pub_t *dhdp)
{
	macdbg_info_t *macdbg_info = dhdp->macdbg_info;
	ASSERT(macdbg_info);

	if (macdbg_info->pd11regs) {
		ASSERT(macdbg_info->d11regs_sz > 0);
		MFREE(dhdp->osh, macdbg_info->pd11regs,
		(macdbg_info->d11regs_sz * sizeof(macdbg_info->pd11regs[0])));
		macdbg_info->d11regs_sz = 0;
	}
	if (macdbg_info->pd11regs_x) {
		ASSERT(macdbg_info->d11regsx_sz > 0);
		MFREE(dhdp->osh, macdbg_info->pd11regs_x,
		(macdbg_info->d11regsx_sz * sizeof(macdbg_info->pd11regs_x[0])));
		macdbg_info->d11regsx_sz = 0;
	}
	if (macdbg_info->psvmpmems) {
		ASSERT(macdbg_info->svmpmems_sz > 0);
		MFREE(dhdp->osh, macdbg_info->psvmpmems,
		(macdbg_info->svmpmems_sz * sizeof(macdbg_info->psvmpmems[0])));
		macdbg_info->svmpmems_sz = 0;
	}
	MFREE(dhdp->osh, macdbg_info, sizeof(*macdbg_info));
}

void
dhd_macdbg_event_handler(dhd_pub_t *dhdp, uint32 reason,
		uint8 *event_data, uint32 datalen)
{
	d11regs_list_t *pd11regs;
	macdbg_info_t *macdbg_info = dhdp->macdbg_info;
	uint d11regs_sz;

	DHD_TRACE(("%s: reason %d datalen %d\n", __FUNCTION__, reason, datalen));
	switch (reason) {
		case WLC_E_MACDBG_LIST_PSMX:
			/* Fall through */
		case WLC_E_MACDBG_LIST_PSM:
			pd11regs = MALLOCZ(dhdp->osh, datalen);
			if (pd11regs == NULL) {
				DHD_ERROR(("%s: NOMEM for len %d\n", __FUNCTION__, datalen));
				return;
			}
			memcpy(pd11regs, event_data, datalen);
			d11regs_sz = datalen / sizeof(pd11regs[0]);
			DHD_ERROR(("%s: d11regs %p d11regs_sz %d\n",
				__FUNCTION__, pd11regs, d11regs_sz));
			if (reason == WLC_E_MACDBG_LIST_PSM) {
				macdbg_info->pd11regs = pd11regs;
				macdbg_info->d11regs_sz = (uint16)d11regs_sz;
			} else {
				macdbg_info->pd11regs_x = pd11regs;
				macdbg_info->d11regsx_sz = (uint16)d11regs_sz;
			}
			break;
		case WLC_E_MACDBG_REGALL:
#ifdef LINUX
			/* Schedule to work queue as this context could be ISR */
			dhd_schedule_macdbg_dump(dhdp);
#else
			/* Dump PSMr */
			(void) dhd_macdbg_dumpmac(dhdp, NULL, 0, NULL, FALSE);
			/* Dump PSMx */
			(void) dhd_macdbg_dumpmac(dhdp, NULL, 0, NULL, TRUE);
			/* Dump SVMP mems */
			(void) dhd_macdbg_dumpsvmp(dhdp, NULL, 0, NULL);
#endif
			break;
		default:
			DHD_ERROR(("%s: Unknown reason %d\n",
				__FUNCTION__, reason));
	}
	return;
}

static uint16
_dhd_get_ihr16(macdbg_info_t *macdbg_info, uint16 addr, struct bcmstrbuf *b, bool verbose)
{
	sdreg_t sdreg;
	uint16 val;

	sdreg.func = 2;
	sdreg.offset = (0x1000 | addr);
	BUS_IOVAR_OP(macdbg_info->dhdp, "sbreg",
		&sdreg, sizeof(sdreg), &val, sizeof(val), IOV_GET);
	if (verbose) {
		if (b) {
			bcm_bprintf(b, "DEBUG: IHR16: read 0x%08x, size 2, value 0x%04x\n",
				(addr + 0x18001000), val);
		} else {
			printf("DEBUG: IHR16: read 0x%08x, size 2, value 0x%04x\n",
				(addr + 0x18001000), val);
		}
	}
	return val;
}

static uint32
_dhd_get_ihr32(macdbg_info_t *macdbg_info, uint16 addr, struct bcmstrbuf *b, bool verbose)
{
	sdreg_t sdreg;
	uint32 val;

	sdreg.func = 4;
	sdreg.offset = (0x1000 | addr);
	BUS_IOVAR_OP(macdbg_info->dhdp, "sbreg",
		&sdreg, sizeof(sdreg), &val, sizeof(val), IOV_GET);
	if (verbose) {
		if (b) {
			bcm_bprintf(b, "DEBUG: IHR32: read 0x%08x, size 4, value 0x%08x\n",
				(addr + 0x18001000), val);
		} else {
			printf("DEBUG: IHR32: read 0x%08x, size 4, value 0x%08x\n",
				(addr + 0x18001000), val);
		}
	}
	return val;
}

static void
_dhd_set_ihr16(macdbg_info_t *macdbg_info, uint16 addr, uint16 val,
	struct bcmstrbuf *b, bool verbose)
{
	sdreg_t sdreg;

	sdreg.func = 2;
	sdreg.offset = (0x1000 | addr);
	sdreg.value = val;

	if (verbose) {
		if (b) {
			bcm_bprintf(b, "DEBUG: IHR16: write 0x%08x, size 2, value 0x%04x\n",
				(addr + 0x18001000), val);
		} else {
			printf("DEBUG: IHR16: write 0x%08x, size 2, value 0x%04x\n",
				(addr + 0x18001000), val);
		}
	}
	BUS_IOVAR_OP(macdbg_info->dhdp, "sbreg",
		NULL, 0, &sdreg, sizeof(sdreg), IOV_SET);
}

static void
_dhd_set_ihr32(macdbg_info_t *macdbg_info, uint16 addr, uint32 val,
	struct bcmstrbuf *b, bool verbose)
{
	sdreg_t sdreg;

	sdreg.func = 4;
	sdreg.offset = (0x1000 | addr);
	sdreg.value = val;

	if (verbose) {
		if (b) {
			bcm_bprintf(b, "DEBUG: IHR32: write 0x%08x, size 4, value 0x%08x\n",
				(addr + 0x18001000), val);
		} else {
			printf("DEBUG: IHR32: write 0x%08x, size 4, value 0x%08x\n",
				(addr + 0x18001000), val);
		}
	}
	BUS_IOVAR_OP(macdbg_info->dhdp, "sbreg",
		NULL, 0, &sdreg, sizeof(sdreg), IOV_SET);
}

static uint32
_dhd_get_d11obj32(macdbg_info_t *macdbg_info, uint16 objaddr, uint32 sel,
	struct bcmstrbuf *b, bool verbose)
{
	uint32 val;
	sdreg_t sdreg;
	sdreg.func = 4; // 4bytes by default.
	sdreg.offset = 0x1160;

	if (objaddr == 0xffff) {
		if (verbose) {
			goto objaddr_read;
		} else {
			goto objdata_read;
		}
	}

	if (objaddr & 0x3) {
		printf("%s: ERROR! Invalid addr 0x%x\n", __FUNCTION__, objaddr);
	}

	sdreg.value = (sel | (objaddr >> 2));

	if (verbose) {
		if (b) {
			bcm_bprintf(b, "DEBUG: %s: Indirect: write 0x%08x, size %d, value 0x%08x\n",
				(sel & 0x00020000) ? "SCR":"SHM",
				(sdreg.offset + 0x18000000), sdreg.func, sdreg.value);
		} else {
			printf("DEBUG: %s: Indirect: write 0x%08x, size %d, value 0x%08x\n",
				(sel & 0x00020000) ? "SCR":"SHM",
				(sdreg.offset + 0x18000000), sdreg.func, sdreg.value);
		}
	}
	BUS_IOVAR_OP(macdbg_info->dhdp, "sbreg",
		NULL, 0, &sdreg, sizeof(sdreg), IOV_SET);

objaddr_read:
	/* Give some time to obj addr register */
	BUS_IOVAR_OP(macdbg_info->dhdp, "sbreg",
		&sdreg, sizeof(sdreg), &val, sizeof(val), IOV_GET);
	if (verbose) {
		if (b) {
			bcm_bprintf(b, "DEBUG: %s: Indirect: Read 0x%08x, size %d, value 0x%08x\n",
				(sel & 0x00020000) ? "SCR":"SHM",
				(sdreg.offset + 0x18000000), sdreg.func, val);
		} else {
			printf("DEBUG: %s: Indirect: Read 0x%08x, size %d, value 0x%08x\n",
				(sel & 0x00020000) ? "SCR":"SHM",
				(sdreg.offset + 0x18000000), sdreg.func, val);
		}
	}

objdata_read:
	sdreg.offset = 0x1164;
	BUS_IOVAR_OP(macdbg_info->dhdp, "sbreg",
		&sdreg, sizeof(sdreg), &val, sizeof(val), IOV_GET);
	if (verbose) {
		if (b) {
			bcm_bprintf(b, "DEBUG: %s: Indirect: Read 0x%08x, size %d, value 0x%04x\n",
				(sel & 0x00020000) ? "SCR":"SHM",
				(sdreg.offset + 0x18000000), sdreg.func, val);
		} else {
			printf("DEBUG: %s: Indirect: Read 0x%08x, size %d, value 0x%04x\n",
				(sel & 0x00020000) ? "SCR":"SHM",
				(sdreg.offset + 0x18000000), sdreg.func, val);
		}
	}
	return val;
}

static uint16
_dhd_get_d11obj16(macdbg_info_t *macdbg_info, uint16 objaddr,
	uint32 sel, d11obj_cache_t *obj_cache, struct bcmstrbuf *b, bool verbose)
{
	uint32 val;
	if (obj_cache && obj_cache->cache_valid && ((obj_cache->sel ^ sel) & (0xffffff)) == 0) {
		if (obj_cache->addr32 == (objaddr & ~0x3)) {
			/* XXX: Same objaddr read as the previous one */
			if (verbose) {
				if (b) {
					bcm_bprintf(b, "DEBUG: %s: Read cache value: "
						"addr32 0x%04x, sel 0x%08x, value 0x%08x\n",
						(sel & 0x00020000) ? "SCR":"SHM",
						obj_cache->addr32, obj_cache->sel, obj_cache->val);
				} else {
					printf("DEBUG: %s: Read cache value: "
						"addr32 0x%04x, sel 0x%08x, value 0x%08x\n",
						(sel & 0x00020000) ? "SCR":"SHM",
						obj_cache->addr32, obj_cache->sel, obj_cache->val);
				}
			}
			val = obj_cache->val;
			goto exit;
		} else if ((obj_cache->sel & 0x02000000) &&
			(obj_cache->addr32 + 4 == (objaddr & ~0x3))) {
			/* XXX: objaddr is auto incrementing, so just read objdata */
			if (verbose) {
				if (b) {
					bcm_bprintf(b, "DEBUG: %s: Read objdata only: "
						"addr32 0x%04x, sel 0x%08x, value 0x%08x\n",
						(sel & 0x00020000) ? "SCR":"SHM",
						obj_cache->addr32, obj_cache->sel, obj_cache->val);
				} else {
					printf("DEBUG: %s: Read objdata only: "
						"addr32 0x%04x, sel 0x%08x, value 0x%08x\n",
						(sel & 0x00020000) ? "SCR":"SHM",
						obj_cache->addr32, obj_cache->sel, obj_cache->val);
				}
			}
			val = _dhd_get_d11obj32(macdbg_info, 0xffff, sel, b, verbose);
			goto exit;
		}
	}
	val = _dhd_get_d11obj32(macdbg_info, (objaddr & ~0x2), sel, b, verbose);
exit:
	if (obj_cache) {
		obj_cache->addr32 = (objaddr & ~0x3);
		obj_cache->sel = sel;
		obj_cache->val = val;
		obj_cache->cache_valid = TRUE;
	}
	return (uint16)((objaddr & 0x2) ? (val >> 16) : val);
}

static int
_dhd_print_d11reg(macdbg_info_t *macdbg_info, int idx, int type, uint16 addr, struct bcmstrbuf *b,
	d11obj_cache_t *obj_cache, bool verbose)
{
	const char *regname[D11REG_TYPE_MAX] = D11REGTYPENAME;
	uint32 val;

	if (type == D11REG_TYPE_IHR32) {
		if ((addr & 0x3)) {
			printf("%s: ERROR! Invalid addr 0x%x\n", __FUNCTION__, addr);
			addr &= ~0x3;
		}
		val = _dhd_get_ihr32(macdbg_info, addr, b, verbose);
		if (b) {
			bcm_bprintf(b, "%-3d %s 0x%-4x = 0x%-8x\n",
				idx, regname[type], addr, val);
		} else {
			printf("%-3d %s 0x%-4x = 0x%-8x\n",
				idx, regname[type], addr, val);
		}
	} else {
		switch (type) {
		case D11REG_TYPE_IHR16: {
			if ((addr & 0x1)) {
				printf("%s: ERROR! Invalid addr 0x%x\n", __FUNCTION__, addr);
				addr &= ~0x1;
			}
			val = _dhd_get_ihr16(macdbg_info, addr, b, verbose);
			break;
		}
		case D11REG_TYPE_IHRX16:
			val = _dhd_get_d11obj16(macdbg_info, (addr - 0x400) << 1, 0x020b0000,
				obj_cache, b, verbose);
			break;
		case D11REG_TYPE_SCR:
			val = _dhd_get_d11obj16(macdbg_info, addr << 2, 0x02020000,
				obj_cache, b, verbose);
			break;
		case D11REG_TYPE_SCRX:
			val = _dhd_get_d11obj16(macdbg_info, addr << 2, 0x020a0000,
				obj_cache, b, verbose);
			break;
		case D11REG_TYPE_SHM:
			val = _dhd_get_d11obj16(macdbg_info, addr, 0x02010000,
				obj_cache, b, verbose);
			break;
		case D11REG_TYPE_SHMX:
			val = _dhd_get_d11obj16(macdbg_info, addr, 0x02090000,
				obj_cache, b, verbose);
			break;
		default:
			printf("Unrecognized type %d!\n", type);
			return 0;
		}
		if (b) {
			bcm_bprintf(b, "%-3d %s 0x%-4x = 0x%-4x\n",
				idx, regname[type], addr, val);
		} else {
			printf("%-3d %s 0x%-4x = 0x%-4x\n",
				idx, regname[type], addr, val);
		}
	}
	return 1;
}

static int
_dhd_print_d11regs(macdbg_info_t *macdbg_info, d11regs_list_t *pregs,
	int start_idx, struct bcmstrbuf *b, bool verbose)
{
	uint16 addr;
	int idx = 0;
	d11obj_cache_t obj_cache = {0, 0, 0, FALSE};

	addr = pregs->addr;
	if (pregs->type >= D11REG_TYPE_MAX) {
		printf("%s: wrong type %d\n", __FUNCTION__, pregs->type);
		return 0;
	}
	if (pregs->bitmap) {
		while (pregs->bitmap) {
			if (pregs->bitmap && (pregs->bitmap & 0x1)) {
				_dhd_print_d11reg(macdbg_info, (idx + start_idx), pregs->type,
					addr, b, &obj_cache, verbose);
				idx++;
			}
			pregs->bitmap = pregs->bitmap >> 1;
			addr += pregs->step;
		}
	} else {
		for (; idx < pregs->cnt; idx++) {
			_dhd_print_d11reg(macdbg_info, (idx + start_idx), pregs->type,
				addr, b, &obj_cache, verbose);
			addr += pregs->step;
		}
	}
	return idx;
}

static int
_dhd_pd11regs_bylist(macdbg_info_t *macdbg_info, d11regs_list_t *reglist,
	uint16 reglist_sz, struct bcmstrbuf *b)
{
	uint i, idx = 0;

	if (reglist != NULL && reglist_sz > 0) {
		for (i = 0; i < reglist_sz; i++) {
			DHD_TRACE(("%s %d %p %d\n", __FUNCTION__, __LINE__,
				&reglist[i], reglist_sz));
			idx += _dhd_print_d11regs(macdbg_info, &reglist[i], idx, b, FALSE);
		}
	}
	return idx;
}

int
dhd_macdbg_dumpmac(dhd_pub_t *dhdp, char *buf, int buflen,
	int *outbuflen, bool dump_x)
{
	macdbg_info_t *macdbg_info = dhdp->macdbg_info;
	struct bcmstrbuf *b = NULL;
	struct bcmstrbuf bcmstrbuf;
	uint cnt = 0;

	DHD_TRACE(("%s %d %p %d %p %d %p %d\n",	__FUNCTION__, __LINE__,
		buf, buflen, macdbg_info->pd11regs, macdbg_info->d11regs_sz,
		macdbg_info->pd11regs_x, macdbg_info->d11regsx_sz));

	if (buf && buflen > 0) {
		bcm_binit(&bcmstrbuf, buf, buflen);
		b = &bcmstrbuf;
	}
	if (!dump_x) {
		/* Dump PSMr */
		cnt += _dhd_pd11regs_bylist(macdbg_info, macdbg_info->pd11regs,
			macdbg_info->d11regs_sz, b);
	} else {
		/* Dump PSMx */
		cnt += _dhd_pd11regs_bylist(macdbg_info, macdbg_info->pd11regs_x,
			macdbg_info->d11regsx_sz, b);
	}

	if (b && outbuflen) {
		if ((uint)buflen > BCMSTRBUF_LEN(b)) {
			*outbuflen = buflen - BCMSTRBUF_LEN(b);
		} else {
			DHD_ERROR(("%s: buflen insufficient!\n", __FUNCTION__));
			*outbuflen = buflen;
			/* Do not return buftooshort to allow printing macregs we have got */
		}
	}

	return ((cnt > 0) ? BCME_OK : BCME_UNSUPPORTED);
}

int
dhd_macdbg_pd11regs(dhd_pub_t *dhdp, char *params, int plen, char *buf, int buflen)
{
	macdbg_info_t *macdbg_info = dhdp->macdbg_info;
	dhd_pd11regs_param *pd11regs = (void *)params;
	dhd_pd11regs_buf *pd11regs_buf = (void *)buf;
	uint16 start_idx;
	bool verbose;
	d11regs_list_t reglist;
	struct bcmstrbuf *b = NULL;
	struct bcmstrbuf bcmstrbuf;

	start_idx = pd11regs->start_idx;
	verbose = pd11regs->verbose;
	memcpy(&reglist, pd11regs->plist, sizeof(reglist));
	memset(buf, '\0', buflen);
	bcm_binit(&bcmstrbuf, (char *)(pd11regs_buf->pbuf),
		(buflen - OFFSETOF(dhd_pd11regs_buf, pbuf)));
	b = &bcmstrbuf;
	pd11regs_buf->idx = (uint16)_dhd_print_d11regs(macdbg_info, &reglist,
		start_idx, b, verbose);

	return ((pd11regs_buf->idx > 0) ? BCME_OK : BCME_ERROR);
}

int
dhd_macdbg_reglist(dhd_pub_t *dhdp, char *buf, int buflen)
{
	int err, desc_idx = 0;
	dhd_maclist_t *maclist = (dhd_maclist_t *)buf;
	macdbg_info_t *macdbg_info = dhdp->macdbg_info;
	void *xtlvbuf_p = maclist->plist;
	uint16 xtlvbuflen = (uint16)buflen;
	xtlv_desc_t xtlv_desc[] = {
		{0, 0, NULL},
		{0, 0, NULL},
		{0, 0, NULL},
		{0, 0, NULL}
	};

	if (!macdbg_info->pd11regs) {
		err = BCME_NOTFOUND;
		goto exit;
	}
	ASSERT(macdbg_info->d11regs_sz > 0);
	xtlv_desc[desc_idx].type = DHD_MACLIST_XTLV_R;
	xtlv_desc[desc_idx].len =
		macdbg_info->d11regs_sz * (uint16)sizeof(*(macdbg_info->pd11regs));
	xtlv_desc[desc_idx].ptr = macdbg_info->pd11regs;
	desc_idx++;

	if (macdbg_info->pd11regs_x) {
		ASSERT(macdbg_info->d11regsx_sz);
		xtlv_desc[desc_idx].type = DHD_MACLIST_XTLV_X;
		xtlv_desc[desc_idx].len = macdbg_info->d11regsx_sz *
			(uint16)sizeof(*(macdbg_info->pd11regs_x));
		xtlv_desc[desc_idx].ptr = macdbg_info->pd11regs_x;
		desc_idx++;
	}

	if (macdbg_info->psvmpmems) {
		ASSERT(macdbg_info->svmpmems_sz);
		xtlv_desc[desc_idx].type = DHD_SVMPLIST_XTLV;
		xtlv_desc[desc_idx].len = macdbg_info->svmpmems_sz *
			(uint16)sizeof(*(macdbg_info->psvmpmems));
		xtlv_desc[desc_idx].ptr = macdbg_info->psvmpmems;
		desc_idx++;
	}

	err = bcm_pack_xtlv_buf_from_mem((uint8 **)&xtlvbuf_p, &xtlvbuflen,
		xtlv_desc, BCM_XTLV_OPTION_ALIGN32);

	maclist->version = 0;		/* No version control for now anyway */
	maclist->bytes_len = (buflen - xtlvbuflen);

exit:
	return err;
}

static int
_dhd_print_svmps(macdbg_info_t *macdbg_info, svmp_list_t *psvmp,
	int start_idx, struct bcmstrbuf *b, bool verbose)
{
	int idx;
	uint32 addr, mem_id, offset, prev_mem_id, prev_offset;
	uint16 cnt, val;

	BCM_REFERENCE(start_idx);

	/* Set tbl ID and tbl offset. */
	_dhd_set_ihr32(macdbg_info, 0x3fc, 0x30000d, b, verbose);
	_dhd_set_ihr32(macdbg_info, 0x3fc, 0x8000000e, b, verbose);

	addr = psvmp->addr;
	cnt = psvmp->cnt;

	/* In validate previous mem_id and offset */
	prev_mem_id = (uint32)(-1);
	prev_offset = (uint32)(-1);

	for (idx = 0; idx < cnt; idx++, addr++) {
		mem_id = (addr >> 15);
		offset = (addr & 0x7fff) >> 1;

		if (mem_id != prev_mem_id) {
			/* Set mem_id */
			_dhd_set_ihr32(macdbg_info, 0x3fc, ((mem_id & 0xffff0000) | 0x10),
				b, verbose);
			_dhd_set_ihr32(macdbg_info, 0x3fc, ((mem_id << 16) | 0xf),
				b, verbose);
		}

		if (offset != prev_offset) {
			/* XXX: Is this needed?
			 * _dhd_set_ihr32(macdbg_info, 0x3fc, 0x30000d, b, verbose);
			 */
			/* svmp offset */
			_dhd_set_ihr32(macdbg_info, 0x3fc, ((offset << 16) | 0xe),
				b, verbose);
		}
		/* Read hi or lo */
		_dhd_set_ihr16(macdbg_info, 0x3fc, ((addr & 0x1) ? 0x10 : 0xf), b, verbose);
		val = _dhd_get_ihr16(macdbg_info, 0x3fe, b, verbose);
		if (b) {
			bcm_bprintf(b, "0x%-4x 0x%-4x\n",
				addr, val);

		} else {
			printf("0x%-4x 0x%-4x\n",
				addr, val);
		}
		prev_mem_id = mem_id;
		prev_offset = offset;
	}
	return idx;
}

static int
_dhd_psvmps_bylist(macdbg_info_t *macdbg_info, svmp_list_t *svmplist,
	uint16 svmplist_sz, struct bcmstrbuf *b)
{
	uint i, idx = 0;

	if (svmplist != NULL && svmplist_sz > 0) {
		for (i = 0; i < svmplist_sz; i++) {
			DHD_TRACE(("%s %d %p %d\n", __FUNCTION__, __LINE__,
				&svmplist[i], svmplist_sz));
			idx += _dhd_print_svmps(macdbg_info, &svmplist[i], idx, b, FALSE);
		}
	}
	return idx;
}

int
dhd_macdbg_dumpsvmp(dhd_pub_t *dhdp, char *buf, int buflen,
	int *outbuflen)
{
	macdbg_info_t *macdbg_info = dhdp->macdbg_info;
	struct bcmstrbuf *b = NULL;
	struct bcmstrbuf bcmstrbuf;
	uint cnt = 0;

	DHD_TRACE(("%s %d %p %d %p %d\n",	__FUNCTION__, __LINE__,
		buf, buflen, macdbg_info->psvmpmems, macdbg_info->svmpmems_sz));

	if (buf && buflen > 0) {
		bcm_binit(&bcmstrbuf, buf, buflen);
		b = &bcmstrbuf;
	}
	cnt = _dhd_psvmps_bylist(macdbg_info, macdbg_info->psvmpmems,
			macdbg_info->svmpmems_sz, b);

	if (b && outbuflen) {
		if ((uint)buflen > BCMSTRBUF_LEN(b)) {
			*outbuflen = buflen - BCMSTRBUF_LEN(b);
		} else {
			DHD_ERROR(("%s: buflen insufficient!\n", __FUNCTION__));
			*outbuflen = buflen;
			/* Do not return buftooshort to allow printing macregs we have got */
		}
	}

	return ((cnt > 0) ? BCME_OK : BCME_UNSUPPORTED);
}

int
dhd_macdbg_psvmpmems(dhd_pub_t *dhdp, char *params, int plen, char *buf, int buflen)
{
	macdbg_info_t *macdbg_info = dhdp->macdbg_info;
	dhd_pd11regs_param *pd11regs = (void *)params;
	dhd_pd11regs_buf *pd11regs_buf = (void *)buf;
	uint16 start_idx;
	bool verbose;
	svmp_list_t reglist;
	struct bcmstrbuf *b = NULL;
	struct bcmstrbuf bcmstrbuf;

	start_idx = pd11regs->start_idx;
	verbose = pd11regs->verbose;
	memcpy(&reglist, pd11regs->plist, sizeof(reglist));
	memset(buf, '\0', buflen);
	bcm_binit(&bcmstrbuf, (char *)(pd11regs_buf->pbuf),
		(buflen - OFFSETOF(dhd_pd11regs_buf, pbuf)));
	b = &bcmstrbuf;
	pd11regs_buf->idx = (uint16)_dhd_print_svmps(macdbg_info, &reglist,
		start_idx, b, verbose);

	return ((pd11regs_buf->idx > 0) ? BCME_OK : BCME_ERROR);
}

#endif /* BCMDBG */

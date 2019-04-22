/*
 * Copyright (c) 2012 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* FWIL is the Firmware Interface Layer. In this module the support functions
 * are located to set and get variables to and from the firmware.
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <brcmu_utils.h>
#include <brcmu_wifi.h>
#include "core.h"
#include "bus.h"
#include "debug.h"
#include "tracepoint.h"
#include "fwil.h"
#include "proto.h"


#define MAX_HEX_DUMP_LEN	64

#ifdef DEBUG
static const char * const brcmf_fil_errstr[] = {
	"BCME_OK",
	"BCME_ERROR",
	"BCME_BADARG",
	"BCME_BADOPTION",
	"BCME_NOTUP",
	"BCME_NOTDOWN",
	"BCME_NOTAP",
	"BCME_NOTSTA",
	"BCME_BADKEYIDX",
	"BCME_RADIOOFF",
	"BCME_NOTBANDLOCKED",
	"BCME_NOCLK",
	"BCME_BADRATESET",
	"BCME_BADBAND",
	"BCME_BUFTOOSHORT",
	"BCME_BUFTOOLONG",
	"BCME_BUSY",
	"BCME_NOTASSOCIATED",
	"BCME_BADSSIDLEN",
	"BCME_OUTOFRANGECHAN",
	"BCME_BADCHAN",
	"BCME_BADADDR",
	"BCME_NORESOURCE",
	"BCME_UNSUPPORTED",
	"BCME_BADLEN",
	"BCME_NOTREADY",
	"BCME_EPERM",
	"BCME_NOMEM",
	"BCME_ASSOCIATED",
	"BCME_RANGE",
	"BCME_NOTFOUND",
	"BCME_WME_NOT_ENABLED",
	"BCME_TSPEC_NOTFOUND",
	"BCME_ACM_NOTSUPPORTED",
	"BCME_NOT_WME_ASSOCIATION",
	"BCME_SDIO_ERROR",
	"BCME_DONGLE_DOWN",
	"BCME_VERSION",
	"BCME_TXFAIL",
	"BCME_RXFAIL",
	"BCME_NODEVICE",
	"BCME_NMODE_DISABLED",
	"BCME_NONRESIDENT",
	"BCME_SCANREJECT",
	"BCME_USAGE_ERROR",
	"BCME_IOCTL_ERROR",
	"BCME_SERIAL_PORT_ERR",
	"BCME_DISABLED",
	"BCME_DECERR",
	"BCME_ENCERR",
	"BCME_MICERR",
	"BCME_REPLAY",
	"BCME_IE_NOTFOUND",
};

static const char *brcmf_fil_get_errstr(u32 err)
{
	if (err >= ARRAY_SIZE(brcmf_fil_errstr))
		return "(unknown)";

	return brcmf_fil_errstr[err];
}
#else
static const char *brcmf_fil_get_errstr(u32 err)
{
	return "";
}
#endif /* DEBUG */

static s32
brcmf_fil_cmd_data(struct brcmf_if *ifp, u32 cmd, void *data, u32 len, bool set)
{
	struct brcmf_pub *drvr = ifp->drvr;
	s32 err, fwerr;

	if (drvr->bus_if->state != BRCMF_BUS_UP) {
		bphy_err(drvr, "bus is down. we have nothing to do.\n");
		return -EIO;
	}

	if (data != NULL)
		len = min_t(uint, len, BRCMF_DCMD_MAXLEN);
	if (set)
		err = brcmf_proto_set_dcmd(drvr, ifp->ifidx, cmd,
					   data, len, &fwerr);
	else
		err = brcmf_proto_query_dcmd(drvr, ifp->ifidx, cmd,
					     data, len, &fwerr);

	if (err) {
		brcmf_dbg(FIL, "Failed: error=%d\n", err);
	} else if (fwerr < 0) {
		brcmf_dbg(FIL, "Firmware error: %s (%d)\n",
			  brcmf_fil_get_errstr((u32)(-fwerr)), fwerr);
		err = -EBADE;
	}
	if (ifp->fwil_fwerr)
		return fwerr;

	return err;
}

s32
brcmf_fil_cmd_data_set(struct brcmf_if *ifp, u32 cmd, void *data, u32 len)
{
	s32 err;

	mutex_lock(&ifp->drvr->proto_block);

	brcmf_dbg(FIL, "ifidx=%d, cmd=%d, len=%d\n", ifp->ifidx, cmd, len);
	brcmf_dbg_hex_dump(BRCMF_FIL_ON(), data,
			   min_t(uint, len, MAX_HEX_DUMP_LEN), "data\n");

	err = brcmf_fil_cmd_data(ifp, cmd, data, len, true);
	mutex_unlock(&ifp->drvr->proto_block);

	return err;
}

s32
brcmf_fil_cmd_data_get(struct brcmf_if *ifp, u32 cmd, void *data, u32 len)
{
	s32 err;

	mutex_lock(&ifp->drvr->proto_block);
	err = brcmf_fil_cmd_data(ifp, cmd, data, len, false);

	brcmf_dbg(FIL, "ifidx=%d, cmd=%d, len=%d\n", ifp->ifidx, cmd, len);
	brcmf_dbg_hex_dump(BRCMF_FIL_ON(), data,
			   min_t(uint, len, MAX_HEX_DUMP_LEN), "data\n");

	mutex_unlock(&ifp->drvr->proto_block);

	return err;
}


s32
brcmf_fil_cmd_int_set(struct brcmf_if *ifp, u32 cmd, u32 data)
{
	s32 err;
	__le32 data_le = cpu_to_le32(data);

	mutex_lock(&ifp->drvr->proto_block);
	brcmf_dbg(FIL, "ifidx=%d, cmd=%d, value=%d\n", ifp->ifidx, cmd, data);
	err = brcmf_fil_cmd_data(ifp, cmd, &data_le, sizeof(data_le), true);
	mutex_unlock(&ifp->drvr->proto_block);

	return err;
}

s32
brcmf_fil_cmd_int_get(struct brcmf_if *ifp, u32 cmd, u32 *data)
{
	s32 err;
	__le32 data_le = cpu_to_le32(*data);

	mutex_lock(&ifp->drvr->proto_block);
	err = brcmf_fil_cmd_data(ifp, cmd, &data_le, sizeof(data_le), false);
	mutex_unlock(&ifp->drvr->proto_block);
	*data = le32_to_cpu(data_le);
	brcmf_dbg(FIL, "ifidx=%d, cmd=%d, value=%d\n", ifp->ifidx, cmd, *data);

	return err;
}

static u32
brcmf_create_iovar(char *name, const char *data, u32 datalen,
		   char *buf, u32 buflen)
{
	u32 len;

	len = strlen(name) + 1;

	if ((len + datalen) > buflen)
		return 0;

	memcpy(buf, name, len);

	/* append data onto the end of the name string */
	if (data && datalen)
		memcpy(&buf[len], data, datalen);

	return len + datalen;
}


s32
brcmf_fil_iovar_data_set(struct brcmf_if *ifp, char *name, const void *data,
			 u32 len)
{
	struct brcmf_pub *drvr = ifp->drvr;
	s32 err;
	u32 buflen;

	mutex_lock(&drvr->proto_block);

	brcmf_dbg(FIL, "ifidx=%d, name=%s, len=%d\n", ifp->ifidx, name, len);
	brcmf_dbg_hex_dump(BRCMF_FIL_ON(), data,
			   min_t(uint, len, MAX_HEX_DUMP_LEN), "data\n");

	buflen = brcmf_create_iovar(name, data, len, drvr->proto_buf,
				    sizeof(drvr->proto_buf));
	if (buflen) {
		err = brcmf_fil_cmd_data(ifp, BRCMF_C_SET_VAR, drvr->proto_buf,
					 buflen, true);
	} else {
		err = -EPERM;
		bphy_err(drvr, "Creating iovar failed\n");
	}

	mutex_unlock(&drvr->proto_block);
	return err;
}

s32
brcmf_fil_iovar_data_get(struct brcmf_if *ifp, char *name, void *data,
			 u32 len)
{
	struct brcmf_pub *drvr = ifp->drvr;
	s32 err;
	u32 buflen;

	mutex_lock(&drvr->proto_block);

	buflen = brcmf_create_iovar(name, data, len, drvr->proto_buf,
				    sizeof(drvr->proto_buf));
	if (buflen) {
		err = brcmf_fil_cmd_data(ifp, BRCMF_C_GET_VAR, drvr->proto_buf,
					 buflen, false);
		if (err == 0)
			memcpy(data, drvr->proto_buf, len);
	} else {
		err = -EPERM;
		bphy_err(drvr, "Creating iovar failed\n");
	}

	brcmf_dbg(FIL, "ifidx=%d, name=%s, len=%d\n", ifp->ifidx, name, len);
	brcmf_dbg_hex_dump(BRCMF_FIL_ON(), data,
			   min_t(uint, len, MAX_HEX_DUMP_LEN), "data\n");

	mutex_unlock(&drvr->proto_block);
	return err;
}

s32
brcmf_fil_iovar_int_set(struct brcmf_if *ifp, char *name, u32 data)
{
	__le32 data_le = cpu_to_le32(data);

	return brcmf_fil_iovar_data_set(ifp, name, &data_le, sizeof(data_le));
}

s32
brcmf_fil_iovar_int_get(struct brcmf_if *ifp, char *name, u32 *data)
{
	__le32 data_le = cpu_to_le32(*data);
	s32 err;

	err = brcmf_fil_iovar_data_get(ifp, name, &data_le, sizeof(data_le));
	if (err == 0)
		*data = le32_to_cpu(data_le);
	return err;
}

static u32
brcmf_create_bsscfg(s32 bsscfgidx, char *name, char *data, u32 datalen,
		    char *buf, u32 buflen)
{
	const s8 *prefix = "bsscfg:";
	s8 *p;
	u32 prefixlen;
	u32 namelen;
	u32 iolen;
	__le32 bsscfgidx_le;

	if (bsscfgidx == 0)
		return brcmf_create_iovar(name, data, datalen, buf, buflen);

	prefixlen = strlen(prefix);
	namelen = strlen(name) + 1; /* lengh of iovar  name + null */
	iolen = prefixlen + namelen + sizeof(bsscfgidx_le) + datalen;

	if (buflen < iolen) {
		brcmf_err("buffer is too short\n");
		return 0;
	}

	p = buf;

	/* copy prefix, no null */
	memcpy(p, prefix, prefixlen);
	p += prefixlen;

	/* copy iovar name including null */
	memcpy(p, name, namelen);
	p += namelen;

	/* bss config index as first data */
	bsscfgidx_le = cpu_to_le32(bsscfgidx);
	memcpy(p, &bsscfgidx_le, sizeof(bsscfgidx_le));
	p += sizeof(bsscfgidx_le);

	/* parameter buffer follows */
	if (datalen)
		memcpy(p, data, datalen);

	return iolen;
}

s32
brcmf_fil_bsscfg_data_set(struct brcmf_if *ifp, char *name,
			  void *data, u32 len)
{
	struct brcmf_pub *drvr = ifp->drvr;
	s32 err;
	u32 buflen;

	mutex_lock(&drvr->proto_block);

	brcmf_dbg(FIL, "ifidx=%d, bsscfgidx=%d, name=%s, len=%d\n", ifp->ifidx,
		  ifp->bsscfgidx, name, len);
	brcmf_dbg_hex_dump(BRCMF_FIL_ON(), data,
			   min_t(uint, len, MAX_HEX_DUMP_LEN), "data\n");

	buflen = brcmf_create_bsscfg(ifp->bsscfgidx, name, data, len,
				     drvr->proto_buf, sizeof(drvr->proto_buf));
	if (buflen) {
		err = brcmf_fil_cmd_data(ifp, BRCMF_C_SET_VAR, drvr->proto_buf,
					 buflen, true);
	} else {
		err = -EPERM;
		bphy_err(drvr, "Creating bsscfg failed\n");
	}

	mutex_unlock(&drvr->proto_block);
	return err;
}

s32
brcmf_fil_bsscfg_data_get(struct brcmf_if *ifp, char *name,
			  void *data, u32 len)
{
	struct brcmf_pub *drvr = ifp->drvr;
	s32 err;
	u32 buflen;

	mutex_lock(&drvr->proto_block);

	buflen = brcmf_create_bsscfg(ifp->bsscfgidx, name, data, len,
				     drvr->proto_buf, sizeof(drvr->proto_buf));
	if (buflen) {
		err = brcmf_fil_cmd_data(ifp, BRCMF_C_GET_VAR, drvr->proto_buf,
					 buflen, false);
		if (err == 0)
			memcpy(data, drvr->proto_buf, len);
	} else {
		err = -EPERM;
		bphy_err(drvr, "Creating bsscfg failed\n");
	}
	brcmf_dbg(FIL, "ifidx=%d, bsscfgidx=%d, name=%s, len=%d\n", ifp->ifidx,
		  ifp->bsscfgidx, name, len);
	brcmf_dbg_hex_dump(BRCMF_FIL_ON(), data,
			   min_t(uint, len, MAX_HEX_DUMP_LEN), "data\n");

	mutex_unlock(&drvr->proto_block);
	return err;

}

s32
brcmf_fil_bsscfg_int_set(struct brcmf_if *ifp, char *name, u32 data)
{
	__le32 data_le = cpu_to_le32(data);

	return brcmf_fil_bsscfg_data_set(ifp, name, &data_le,
					 sizeof(data_le));
}

s32
brcmf_fil_bsscfg_int_get(struct brcmf_if *ifp, char *name, u32 *data)
{
	__le32 data_le = cpu_to_le32(*data);
	s32 err;

	err = brcmf_fil_bsscfg_data_get(ifp, name, &data_le,
					sizeof(data_le));
	if (err == 0)
		*data = le32_to_cpu(data_le);
	return err;
}

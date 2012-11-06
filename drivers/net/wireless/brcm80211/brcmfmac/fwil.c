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
#include <defs.h>
#include <brcmu_utils.h>
#include <brcmu_wifi.h>
#include "dhd.h"
#include "dhd_bus.h"
#include "dhd_dbg.h"
#include "fwil.h"


static s32
brcmf_fil_cmd_data(struct brcmf_if *ifp, u32 cmd, void *data, u32 len, bool set)
{
	struct brcmf_pub *drvr = ifp->drvr;
	s32 err;

	if (drvr->bus_if->state == BRCMF_BUS_DOWN) {
		brcmf_dbg(ERROR, "bus is down. we have nothing to do.\n");
		return -EIO;
	}

	if (data != NULL)
		len = min_t(uint, len, BRCMF_DCMD_MAXLEN);
	if (set)
		err = brcmf_proto_cdc_set_dcmd(drvr, ifp->idx, cmd, data, len);
	else
		err = brcmf_proto_cdc_query_dcmd(drvr, ifp->idx, cmd, data,
						 len);

	if (err >= 0)
		err = 0;
	else
		brcmf_dbg(ERROR, "Failed err=%d\n", err);

	return err;
}

s32
brcmf_fil_cmd_data_set(struct brcmf_if *ifp, u32 cmd, void *data, u32 len)
{
	s32 err;

	mutex_lock(&ifp->drvr->proto_block);

	brcmf_dbg(FIL, "cmd=%d, len=%d\n", cmd, len);
	brcmf_dbg_hex_dump(BRCMF_FIL_ON(), data, len, "data");

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

	brcmf_dbg(FIL, "cmd=%d, len=%d\n", cmd, len);
	brcmf_dbg_hex_dump(BRCMF_FIL_ON(), data, len, "data");

	mutex_unlock(&ifp->drvr->proto_block);

	return err;
}


s32
brcmf_fil_cmd_int_set(struct brcmf_if *ifp, u32 cmd, u32 data)
{
	s32 err;
	__le32 data_le = cpu_to_le32(data);

	mutex_lock(&ifp->drvr->proto_block);
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

	return err;
}

static u32
brcmf_create_iovar(char *name, char *data, u32 datalen, char *buf, u32 buflen)
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
brcmf_fil_iovar_data_set(struct brcmf_if *ifp, char *name, void *data,
			 u32 len)
{
	struct brcmf_pub *drvr = ifp->drvr;
	s32 err;
	u32 buflen;

	mutex_lock(&drvr->proto_block);

	brcmf_dbg(FIL, "name=%s, len=%d\n", name, len);
	brcmf_dbg_hex_dump(BRCMF_FIL_ON(), data, len, "data");

	buflen = brcmf_create_iovar(name, data, len, drvr->proto_buf,
				    sizeof(drvr->proto_buf));
	if (buflen) {
		err = brcmf_fil_cmd_data(ifp, BRCMF_C_SET_VAR, drvr->proto_buf,
					 buflen, true);
	} else {
		err = -EPERM;
		brcmf_dbg(ERROR, "Creating iovar failed\n");
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
		brcmf_dbg(ERROR, "Creating iovar failed\n");
	}

	brcmf_dbg(FIL, "name=%s, len=%d\n", name, len);
	brcmf_dbg_hex_dump(BRCMF_FIL_ON(), data, len, "data");

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
brcmf_create_bsscfg(s32 bssidx, char *name, char *data, u32 datalen, char *buf,
		    u32 buflen)
{
	const s8 *prefix = "bsscfg:";
	s8 *p;
	u32 prefixlen;
	u32 namelen;
	u32 iolen;
	__le32 bssidx_le;

	if (bssidx == 0)
		return brcmf_create_iovar(name, data, datalen, buf, buflen);

	prefixlen = strlen(prefix);
	namelen = strlen(name) + 1; /* lengh of iovar  name + null */
	iolen = prefixlen + namelen + sizeof(bssidx_le) + datalen;

	if (buflen < iolen) {
		brcmf_dbg(ERROR, "buffer is too short\n");
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
	bssidx_le = cpu_to_le32(bssidx);
	memcpy(p, &bssidx_le, sizeof(bssidx_le));
	p += sizeof(bssidx_le);

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

	brcmf_dbg(FIL, "bssidx=%d, name=%s, len=%d\n", ifp->bssidx, name, len);
	brcmf_dbg_hex_dump(BRCMF_FIL_ON(), data, len, "data");

	buflen = brcmf_create_bsscfg(ifp->bssidx, name, data, len,
				     drvr->proto_buf, sizeof(drvr->proto_buf));
	if (buflen) {
		err = brcmf_fil_cmd_data(ifp, BRCMF_C_SET_VAR, drvr->proto_buf,
					 buflen, true);
	} else {
		err = -EPERM;
		brcmf_dbg(ERROR, "Creating bsscfg failed\n");
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

	buflen = brcmf_create_bsscfg(ifp->bssidx, name, data, len,
				     drvr->proto_buf, sizeof(drvr->proto_buf));
	if (buflen) {
		err = brcmf_fil_cmd_data(ifp, BRCMF_C_GET_VAR, drvr->proto_buf,
					 buflen, false);
		if (err == 0)
			memcpy(data, drvr->proto_buf, len);
	} else {
		err = -EPERM;
		brcmf_dbg(ERROR, "Creating bsscfg failed\n");
	}
	brcmf_dbg(FIL, "bssidx=%d, name=%s, len=%d\n", ifp->bssidx, name, len);
	brcmf_dbg_hex_dump(BRCMF_FIL_ON(), data, len, "data");

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

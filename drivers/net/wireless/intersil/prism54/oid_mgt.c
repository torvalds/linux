/*
 *  Copyright (C) 2003,2004 Aurelien Alleaume <slts@free.fr>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>

#include "prismcompat.h"
#include "islpci_dev.h"
#include "islpci_mgt.h"
#include "isl_oid.h"
#include "oid_mgt.h"
#include "isl_ioctl.h"

/* to convert between channel and freq */
static const int frequency_list_bg[] = { 2412, 2417, 2422, 2427, 2432,
	2437, 2442, 2447, 2452, 2457, 2462, 2467, 2472, 2484
};

int
channel_of_freq(int f)
{
	int c = 0;

	if ((f >= 2412) && (f <= 2484)) {
		while ((c < 14) && (f != frequency_list_bg[c]))
			c++;
		return (c >= 14) ? 0 : ++c;
	} else if ((f >= (int) 5000) && (f <= (int) 6000)) {
		return ( (f - 5000) / 5 );
	} else
		return 0;
}

#define OID_STRUCT(name,oid,s,t) [name] = {oid, 0, sizeof(s), t}
#define OID_STRUCT_C(name,oid,s,t) OID_STRUCT(name,oid,s,t | OID_FLAG_CACHED)
#define OID_U32(name,oid) OID_STRUCT(name,oid,u32,OID_TYPE_U32)
#define OID_U32_C(name,oid) OID_STRUCT_C(name,oid,u32,OID_TYPE_U32)
#define OID_STRUCT_MLME(name,oid) OID_STRUCT(name,oid,struct obj_mlme,OID_TYPE_MLME)
#define OID_STRUCT_MLMEEX(name,oid) OID_STRUCT(name,oid,struct obj_mlmeex,OID_TYPE_MLMEEX)

#define OID_UNKNOWN(name,oid) OID_STRUCT(name,oid,0,0)

struct oid_t isl_oid[] = {
	OID_STRUCT(GEN_OID_MACADDRESS, 0x00000000, u8[6], OID_TYPE_ADDR),
	OID_U32(GEN_OID_LINKSTATE, 0x00000001),
	OID_UNKNOWN(GEN_OID_WATCHDOG, 0x00000002),
	OID_UNKNOWN(GEN_OID_MIBOP, 0x00000003),
	OID_UNKNOWN(GEN_OID_OPTIONS, 0x00000004),
	OID_UNKNOWN(GEN_OID_LEDCONFIG, 0x00000005),

	/* 802.11 */
	OID_U32_C(DOT11_OID_BSSTYPE, 0x10000000),
	OID_STRUCT_C(DOT11_OID_BSSID, 0x10000001, u8[6], OID_TYPE_RAW),
	OID_STRUCT_C(DOT11_OID_SSID, 0x10000002, struct obj_ssid,
		     OID_TYPE_SSID),
	OID_U32(DOT11_OID_STATE, 0x10000003),
	OID_U32(DOT11_OID_AID, 0x10000004),
	OID_STRUCT(DOT11_OID_COUNTRYSTRING, 0x10000005, u8[4], OID_TYPE_RAW),
	OID_STRUCT_C(DOT11_OID_SSIDOVERRIDE, 0x10000006, struct obj_ssid,
		     OID_TYPE_SSID),

	OID_U32(DOT11_OID_MEDIUMLIMIT, 0x11000000),
	OID_U32_C(DOT11_OID_BEACONPERIOD, 0x11000001),
	OID_U32(DOT11_OID_DTIMPERIOD, 0x11000002),
	OID_U32(DOT11_OID_ATIMWINDOW, 0x11000003),
	OID_U32(DOT11_OID_LISTENINTERVAL, 0x11000004),
	OID_U32(DOT11_OID_CFPPERIOD, 0x11000005),
	OID_U32(DOT11_OID_CFPDURATION, 0x11000006),

	OID_U32_C(DOT11_OID_AUTHENABLE, 0x12000000),
	OID_U32_C(DOT11_OID_PRIVACYINVOKED, 0x12000001),
	OID_U32_C(DOT11_OID_EXUNENCRYPTED, 0x12000002),
	OID_U32_C(DOT11_OID_DEFKEYID, 0x12000003),
	[DOT11_OID_DEFKEYX] = {0x12000004, 3, sizeof (struct obj_key),
			       OID_FLAG_CACHED | OID_TYPE_KEY},	/* DOT11_OID_DEFKEY1,...DOT11_OID_DEFKEY4 */
	OID_UNKNOWN(DOT11_OID_STAKEY, 0x12000008),
	OID_U32(DOT11_OID_REKEYTHRESHOLD, 0x12000009),
	OID_UNKNOWN(DOT11_OID_STASC, 0x1200000a),

	OID_U32(DOT11_OID_PRIVTXREJECTED, 0x1a000000),
	OID_U32(DOT11_OID_PRIVRXPLAIN, 0x1a000001),
	OID_U32(DOT11_OID_PRIVRXFAILED, 0x1a000002),
	OID_U32(DOT11_OID_PRIVRXNOKEY, 0x1a000003),

	OID_U32_C(DOT11_OID_RTSTHRESH, 0x13000000),
	OID_U32_C(DOT11_OID_FRAGTHRESH, 0x13000001),
	OID_U32_C(DOT11_OID_SHORTRETRIES, 0x13000002),
	OID_U32_C(DOT11_OID_LONGRETRIES, 0x13000003),
	OID_U32_C(DOT11_OID_MAXTXLIFETIME, 0x13000004),
	OID_U32(DOT11_OID_MAXRXLIFETIME, 0x13000005),
	OID_U32(DOT11_OID_AUTHRESPTIMEOUT, 0x13000006),
	OID_U32(DOT11_OID_ASSOCRESPTIMEOUT, 0x13000007),

	OID_UNKNOWN(DOT11_OID_ALOFT_TABLE, 0x1d000000),
	OID_UNKNOWN(DOT11_OID_ALOFT_CTRL_TABLE, 0x1d000001),
	OID_UNKNOWN(DOT11_OID_ALOFT_RETREAT, 0x1d000002),
	OID_UNKNOWN(DOT11_OID_ALOFT_PROGRESS, 0x1d000003),
	OID_U32(DOT11_OID_ALOFT_FIXEDRATE, 0x1d000004),
	OID_UNKNOWN(DOT11_OID_ALOFT_RSSIGRAPH, 0x1d000005),
	OID_UNKNOWN(DOT11_OID_ALOFT_CONFIG, 0x1d000006),

	[DOT11_OID_VDCFX] = {0x1b000000, 7, 0, 0},
	OID_U32(DOT11_OID_MAXFRAMEBURST, 0x1b000008),

	OID_U32(DOT11_OID_PSM, 0x14000000),
	OID_U32(DOT11_OID_CAMTIMEOUT, 0x14000001),
	OID_U32(DOT11_OID_RECEIVEDTIMS, 0x14000002),
	OID_U32(DOT11_OID_ROAMPREFERENCE, 0x14000003),

	OID_U32(DOT11_OID_BRIDGELOCAL, 0x15000000),
	OID_U32(DOT11_OID_CLIENTS, 0x15000001),
	OID_U32(DOT11_OID_CLIENTSASSOCIATED, 0x15000002),
	[DOT11_OID_CLIENTX] = {0x15000003, 2006, 0, 0},	/* DOT11_OID_CLIENTX,...DOT11_OID_CLIENT2007 */

	OID_STRUCT(DOT11_OID_CLIENTFIND, 0x150007DB, u8[6], OID_TYPE_ADDR),
	OID_STRUCT(DOT11_OID_WDSLINKADD, 0x150007DC, u8[6], OID_TYPE_ADDR),
	OID_STRUCT(DOT11_OID_WDSLINKREMOVE, 0x150007DD, u8[6], OID_TYPE_ADDR),
	OID_STRUCT(DOT11_OID_EAPAUTHSTA, 0x150007DE, u8[6], OID_TYPE_ADDR),
	OID_STRUCT(DOT11_OID_EAPUNAUTHSTA, 0x150007DF, u8[6], OID_TYPE_ADDR),
	OID_U32_C(DOT11_OID_DOT1XENABLE, 0x150007E0),
	OID_UNKNOWN(DOT11_OID_MICFAILURE, 0x150007E1),
	OID_UNKNOWN(DOT11_OID_REKEYINDICATE, 0x150007E2),

	OID_U32(DOT11_OID_MPDUTXSUCCESSFUL, 0x16000000),
	OID_U32(DOT11_OID_MPDUTXONERETRY, 0x16000001),
	OID_U32(DOT11_OID_MPDUTXMULTIPLERETRIES, 0x16000002),
	OID_U32(DOT11_OID_MPDUTXFAILED, 0x16000003),
	OID_U32(DOT11_OID_MPDURXSUCCESSFUL, 0x16000004),
	OID_U32(DOT11_OID_MPDURXDUPS, 0x16000005),
	OID_U32(DOT11_OID_RTSSUCCESSFUL, 0x16000006),
	OID_U32(DOT11_OID_RTSFAILED, 0x16000007),
	OID_U32(DOT11_OID_ACKFAILED, 0x16000008),
	OID_U32(DOT11_OID_FRAMERECEIVES, 0x16000009),
	OID_U32(DOT11_OID_FRAMEERRORS, 0x1600000A),
	OID_U32(DOT11_OID_FRAMEABORTS, 0x1600000B),
	OID_U32(DOT11_OID_FRAMEABORTSPHY, 0x1600000C),

	OID_U32(DOT11_OID_SLOTTIME, 0x17000000),
	OID_U32(DOT11_OID_CWMIN, 0x17000001),
	OID_U32(DOT11_OID_CWMAX, 0x17000002),
	OID_U32(DOT11_OID_ACKWINDOW, 0x17000003),
	OID_U32(DOT11_OID_ANTENNARX, 0x17000004),
	OID_U32(DOT11_OID_ANTENNATX, 0x17000005),
	OID_U32(DOT11_OID_ANTENNADIVERSITY, 0x17000006),
	OID_U32_C(DOT11_OID_CHANNEL, 0x17000007),
	OID_U32_C(DOT11_OID_EDTHRESHOLD, 0x17000008),
	OID_U32(DOT11_OID_PREAMBLESETTINGS, 0x17000009),
	OID_STRUCT(DOT11_OID_RATES, 0x1700000A, u8[IWMAX_BITRATES + 1],
		   OID_TYPE_RAW),
	OID_U32(DOT11_OID_CCAMODESUPPORTED, 0x1700000B),
	OID_U32(DOT11_OID_CCAMODE, 0x1700000C),
	OID_UNKNOWN(DOT11_OID_RSSIVECTOR, 0x1700000D),
	OID_UNKNOWN(DOT11_OID_OUTPUTPOWERTABLE, 0x1700000E),
	OID_U32(DOT11_OID_OUTPUTPOWER, 0x1700000F),
	OID_STRUCT(DOT11_OID_SUPPORTEDRATES, 0x17000010,
		   u8[IWMAX_BITRATES + 1], OID_TYPE_RAW),
	OID_U32_C(DOT11_OID_FREQUENCY, 0x17000011),
	[DOT11_OID_SUPPORTEDFREQUENCIES] =
	    {0x17000012, 0, sizeof (struct obj_frequencies)
	     + sizeof (u16) * IWMAX_FREQ, OID_TYPE_FREQUENCIES},

	OID_U32(DOT11_OID_NOISEFLOOR, 0x17000013),
	OID_STRUCT(DOT11_OID_FREQUENCYACTIVITY, 0x17000014, u8[IWMAX_FREQ + 1],
		   OID_TYPE_RAW),
	OID_UNKNOWN(DOT11_OID_IQCALIBRATIONTABLE, 0x17000015),
	OID_U32(DOT11_OID_NONERPPROTECTION, 0x17000016),
	OID_U32(DOT11_OID_SLOTSETTINGS, 0x17000017),
	OID_U32(DOT11_OID_NONERPTIMEOUT, 0x17000018),
	OID_U32(DOT11_OID_PROFILES, 0x17000019),
	OID_STRUCT(DOT11_OID_EXTENDEDRATES, 0x17000020,
		   u8[IWMAX_BITRATES + 1], OID_TYPE_RAW),

	OID_STRUCT_MLME(DOT11_OID_DEAUTHENTICATE, 0x18000000),
	OID_STRUCT_MLME(DOT11_OID_AUTHENTICATE, 0x18000001),
	OID_STRUCT_MLME(DOT11_OID_DISASSOCIATE, 0x18000002),
	OID_STRUCT_MLME(DOT11_OID_ASSOCIATE, 0x18000003),
	OID_UNKNOWN(DOT11_OID_SCAN, 0x18000004),
	OID_STRUCT_MLMEEX(DOT11_OID_BEACON, 0x18000005),
	OID_STRUCT_MLMEEX(DOT11_OID_PROBE, 0x18000006),
	OID_STRUCT_MLMEEX(DOT11_OID_DEAUTHENTICATEEX, 0x18000007),
	OID_STRUCT_MLMEEX(DOT11_OID_AUTHENTICATEEX, 0x18000008),
	OID_STRUCT_MLMEEX(DOT11_OID_DISASSOCIATEEX, 0x18000009),
	OID_STRUCT_MLMEEX(DOT11_OID_ASSOCIATEEX, 0x1800000A),
	OID_STRUCT_MLMEEX(DOT11_OID_REASSOCIATE, 0x1800000B),
	OID_STRUCT_MLMEEX(DOT11_OID_REASSOCIATEEX, 0x1800000C),

	OID_U32(DOT11_OID_NONERPSTATUS, 0x1E000000),

	OID_U32(DOT11_OID_STATIMEOUT, 0x19000000),
	OID_U32_C(DOT11_OID_MLMEAUTOLEVEL, 0x19000001),
	OID_U32(DOT11_OID_BSSTIMEOUT, 0x19000002),
	[DOT11_OID_ATTACHMENT] = {0x19000003, 0,
		sizeof(struct obj_attachment), OID_TYPE_ATTACH},
	OID_STRUCT_C(DOT11_OID_PSMBUFFER, 0x19000004, struct obj_buffer,
		     OID_TYPE_BUFFER),

	OID_U32(DOT11_OID_BSSS, 0x1C000000),
	[DOT11_OID_BSSX] = {0x1C000001, 63, sizeof (struct obj_bss),
			    OID_TYPE_BSS},	/*DOT11_OID_BSS1,...,DOT11_OID_BSS64 */
	OID_STRUCT(DOT11_OID_BSSFIND, 0x1C000042, struct obj_bss, OID_TYPE_BSS),
	[DOT11_OID_BSSLIST] = {0x1C000043, 0, sizeof (struct
						      obj_bsslist) +
			       sizeof (struct obj_bss[IWMAX_BSS]),
			       OID_TYPE_BSSLIST},

	OID_UNKNOWN(OID_INL_TUNNEL, 0xFF020000),
	OID_UNKNOWN(OID_INL_MEMADDR, 0xFF020001),
	OID_UNKNOWN(OID_INL_MEMORY, 0xFF020002),
	OID_U32_C(OID_INL_MODE, 0xFF020003),
	OID_UNKNOWN(OID_INL_COMPONENT_NR, 0xFF020004),
	OID_STRUCT(OID_INL_VERSION, 0xFF020005, u8[8], OID_TYPE_RAW),
	OID_UNKNOWN(OID_INL_INTERFACE_ID, 0xFF020006),
	OID_UNKNOWN(OID_INL_COMPONENT_ID, 0xFF020007),
	OID_U32_C(OID_INL_CONFIG, 0xFF020008),
	OID_U32_C(OID_INL_DOT11D_CONFORMANCE, 0xFF02000C),
	OID_U32(OID_INL_PHYCAPABILITIES, 0xFF02000D),
	OID_U32_C(OID_INL_OUTPUTPOWER, 0xFF02000F),

};

int
mgt_init(islpci_private *priv)
{
	int i;

	priv->mib = kcalloc(OID_NUM_LAST, sizeof (void *), GFP_KERNEL);
	if (!priv->mib)
		return -ENOMEM;

	/* Alloc the cache */
	for (i = 0; i < OID_NUM_LAST; i++) {
		if (isl_oid[i].flags & OID_FLAG_CACHED) {
			priv->mib[i] = kcalloc(isl_oid[i].size,
					       (isl_oid[i].range + 1),
					       GFP_KERNEL);
			if (!priv->mib[i])
				return -ENOMEM;
		} else
			priv->mib[i] = NULL;
	}

	init_rwsem(&priv->mib_sem);
	prism54_mib_init(priv);

	return 0;
}

void
mgt_clean(islpci_private *priv)
{
	int i;

	if (!priv->mib)
		return;
	for (i = 0; i < OID_NUM_LAST; i++) {
		kfree(priv->mib[i]);
		priv->mib[i] = NULL;
	}
	kfree(priv->mib);
	priv->mib = NULL;
}

void
mgt_le_to_cpu(int type, void *data)
{
	switch (type) {
	case OID_TYPE_U32:
		*(u32 *) data = le32_to_cpu(*(u32 *) data);
		break;
	case OID_TYPE_BUFFER:{
			struct obj_buffer *buff = data;
			buff->size = le32_to_cpu(buff->size);
			buff->addr = le32_to_cpu(buff->addr);
			break;
		}
	case OID_TYPE_BSS:{
			struct obj_bss *bss = data;
			bss->age = le16_to_cpu(bss->age);
			bss->channel = le16_to_cpu(bss->channel);
			bss->capinfo = le16_to_cpu(bss->capinfo);
			bss->rates = le16_to_cpu(bss->rates);
			bss->basic_rates = le16_to_cpu(bss->basic_rates);
			break;
		}
	case OID_TYPE_BSSLIST:{
			struct obj_bsslist *list = data;
			int i;
			list->nr = le32_to_cpu(list->nr);
			for (i = 0; i < list->nr; i++)
				mgt_le_to_cpu(OID_TYPE_BSS, &list->bsslist[i]);
			break;
		}
	case OID_TYPE_FREQUENCIES:{
			struct obj_frequencies *freq = data;
			int i;
			freq->nr = le16_to_cpu(freq->nr);
			for (i = 0; i < freq->nr; i++)
				freq->mhz[i] = le16_to_cpu(freq->mhz[i]);
			break;
		}
	case OID_TYPE_MLME:{
			struct obj_mlme *mlme = data;
			mlme->id = le16_to_cpu(mlme->id);
			mlme->state = le16_to_cpu(mlme->state);
			mlme->code = le16_to_cpu(mlme->code);
			break;
		}
	case OID_TYPE_MLMEEX:{
			struct obj_mlmeex *mlme = data;
			mlme->id = le16_to_cpu(mlme->id);
			mlme->state = le16_to_cpu(mlme->state);
			mlme->code = le16_to_cpu(mlme->code);
			mlme->size = le16_to_cpu(mlme->size);
			break;
		}
	case OID_TYPE_ATTACH:{
			struct obj_attachment *attach = data;
			attach->id = le16_to_cpu(attach->id);
			attach->size = le16_to_cpu(attach->size);
			break;
	}
	case OID_TYPE_SSID:
	case OID_TYPE_KEY:
	case OID_TYPE_ADDR:
	case OID_TYPE_RAW:
		break;
	default:
		BUG();
	}
}

static void
mgt_cpu_to_le(int type, void *data)
{
	switch (type) {
	case OID_TYPE_U32:
		*(u32 *) data = cpu_to_le32(*(u32 *) data);
		break;
	case OID_TYPE_BUFFER:{
			struct obj_buffer *buff = data;
			buff->size = cpu_to_le32(buff->size);
			buff->addr = cpu_to_le32(buff->addr);
			break;
		}
	case OID_TYPE_BSS:{
			struct obj_bss *bss = data;
			bss->age = cpu_to_le16(bss->age);
			bss->channel = cpu_to_le16(bss->channel);
			bss->capinfo = cpu_to_le16(bss->capinfo);
			bss->rates = cpu_to_le16(bss->rates);
			bss->basic_rates = cpu_to_le16(bss->basic_rates);
			break;
		}
	case OID_TYPE_BSSLIST:{
			struct obj_bsslist *list = data;
			int i;
			list->nr = cpu_to_le32(list->nr);
			for (i = 0; i < list->nr; i++)
				mgt_cpu_to_le(OID_TYPE_BSS, &list->bsslist[i]);
			break;
		}
	case OID_TYPE_FREQUENCIES:{
			struct obj_frequencies *freq = data;
			int i;
			freq->nr = cpu_to_le16(freq->nr);
			for (i = 0; i < freq->nr; i++)
				freq->mhz[i] = cpu_to_le16(freq->mhz[i]);
			break;
		}
	case OID_TYPE_MLME:{
			struct obj_mlme *mlme = data;
			mlme->id = cpu_to_le16(mlme->id);
			mlme->state = cpu_to_le16(mlme->state);
			mlme->code = cpu_to_le16(mlme->code);
			break;
		}
	case OID_TYPE_MLMEEX:{
			struct obj_mlmeex *mlme = data;
			mlme->id = cpu_to_le16(mlme->id);
			mlme->state = cpu_to_le16(mlme->state);
			mlme->code = cpu_to_le16(mlme->code);
			mlme->size = cpu_to_le16(mlme->size);
			break;
		}
	case OID_TYPE_ATTACH:{
			struct obj_attachment *attach = data;
			attach->id = cpu_to_le16(attach->id);
			attach->size = cpu_to_le16(attach->size);
			break;
	}
	case OID_TYPE_SSID:
	case OID_TYPE_KEY:
	case OID_TYPE_ADDR:
	case OID_TYPE_RAW:
		break;
	default:
		BUG();
	}
}

/* Note : data is modified during this function */

int
mgt_set_request(islpci_private *priv, enum oid_num_t n, int extra, void *data)
{
	int ret = 0;
	struct islpci_mgmtframe *response = NULL;
	int response_op = PIMFOR_OP_ERROR;
	int dlen;
	void *cache, *_data = data;
	u32 oid;

	BUG_ON(n >= OID_NUM_LAST);
	BUG_ON(extra > isl_oid[n].range);

	if (!priv->mib)
		/* memory has been freed */
		return -1;

	dlen = isl_oid[n].size;
	cache = priv->mib[n];
	cache += (cache ? extra * dlen : 0);
	oid = isl_oid[n].oid + extra;

	if (_data == NULL)
		/* we are requested to re-set a cached value */
		_data = cache;
	else
		mgt_cpu_to_le(isl_oid[n].flags & OID_FLAG_TYPE, _data);
	/* If we are going to write to the cache, we don't want anyone to read
	 * it -> acquire write lock.
	 * Else we could acquire a read lock to be sure we don't bother the
	 * commit process (which takes a write lock). But I'm not sure if it's
	 * needed.
	 */
	if (cache)
		down_write(&priv->mib_sem);

	if (islpci_get_state(priv) >= PRV_STATE_READY) {
		ret = islpci_mgt_transaction(priv->ndev, PIMFOR_OP_SET, oid,
					     _data, dlen, &response);
		if (!ret) {
			response_op = response->header->operation;
			islpci_mgt_release(response);
		}
		if (ret || response_op == PIMFOR_OP_ERROR)
			ret = -EIO;
	} else if (!cache)
		ret = -EIO;

	if (cache) {
		if (!ret && data)
			memcpy(cache, _data, dlen);
		up_write(&priv->mib_sem);
	}

	/* re-set given data to what it was */
	if (data)
		mgt_le_to_cpu(isl_oid[n].flags & OID_FLAG_TYPE, data);

	return ret;
}

/* None of these are cached */
int
mgt_set_varlen(islpci_private *priv, enum oid_num_t n, void *data, int extra_len)
{
	int ret = 0;
	struct islpci_mgmtframe *response;
	int response_op = PIMFOR_OP_ERROR;
	int dlen;
	u32 oid;

	BUG_ON(n >= OID_NUM_LAST);

	dlen = isl_oid[n].size;
	oid = isl_oid[n].oid;

	mgt_cpu_to_le(isl_oid[n].flags & OID_FLAG_TYPE, data);

	if (islpci_get_state(priv) >= PRV_STATE_READY) {
		ret = islpci_mgt_transaction(priv->ndev, PIMFOR_OP_SET, oid,
					     data, dlen + extra_len, &response);
		if (!ret) {
			response_op = response->header->operation;
			islpci_mgt_release(response);
		}
		if (ret || response_op == PIMFOR_OP_ERROR)
			ret = -EIO;
	} else
		ret = -EIO;

	/* re-set given data to what it was */
	if (data)
		mgt_le_to_cpu(isl_oid[n].flags & OID_FLAG_TYPE, data);

	return ret;
}

int
mgt_get_request(islpci_private *priv, enum oid_num_t n, int extra, void *data,
		union oid_res_t *res)
{

	int ret = -EIO;
	int reslen = 0;
	struct islpci_mgmtframe *response = NULL;

	int dlen;
	void *cache, *_res = NULL;
	u32 oid;

	BUG_ON(n >= OID_NUM_LAST);
	BUG_ON(extra > isl_oid[n].range);

	res->ptr = NULL;

	if (!priv->mib)
		/* memory has been freed */
		return -1;

	dlen = isl_oid[n].size;
	cache = priv->mib[n];
	cache += cache ? extra * dlen : 0;
	oid = isl_oid[n].oid + extra;
	reslen = dlen;

	if (cache)
		down_read(&priv->mib_sem);

	if (islpci_get_state(priv) >= PRV_STATE_READY) {
		ret = islpci_mgt_transaction(priv->ndev, PIMFOR_OP_GET,
					     oid, data, dlen, &response);
		if (ret || !response ||
		    response->header->operation == PIMFOR_OP_ERROR) {
			if (response)
				islpci_mgt_release(response);
			ret = -EIO;
		}
		if (!ret) {
			_res = response->data;
			reslen = response->header->length;
		}
	} else if (cache) {
		_res = cache;
		ret = 0;
	}
	if ((isl_oid[n].flags & OID_FLAG_TYPE) == OID_TYPE_U32)
		res->u = ret ? 0 : le32_to_cpu(*(u32 *) _res);
	else {
		res->ptr = kmalloc(reslen, GFP_KERNEL);
		BUG_ON(res->ptr == NULL);
		if (ret)
			memset(res->ptr, 0, reslen);
		else {
			memcpy(res->ptr, _res, reslen);
			mgt_le_to_cpu(isl_oid[n].flags & OID_FLAG_TYPE,
				      res->ptr);
		}
	}
	if (cache)
		up_read(&priv->mib_sem);

	if (response && !ret)
		islpci_mgt_release(response);

	if (reslen > isl_oid[n].size)
		printk(KERN_DEBUG
		       "mgt_get_request(0x%x): received data length was bigger "
		       "than expected (%d > %d). Memory is probably corrupted...",
		       oid, reslen, isl_oid[n].size);

	return ret;
}

/* lock outside */
int
mgt_commit_list(islpci_private *priv, enum oid_num_t *l, int n)
{
	int i, ret = 0;
	struct islpci_mgmtframe *response;

	for (i = 0; i < n; i++) {
		struct oid_t *t = &(isl_oid[l[i]]);
		void *data = priv->mib[l[i]];
		int j = 0;
		u32 oid = t->oid;
		BUG_ON(data == NULL);
		while (j <= t->range) {
			int r = islpci_mgt_transaction(priv->ndev, PIMFOR_OP_SET,
						      oid, data, t->size,
						      &response);
			if (response) {
				r |= (response->header->operation == PIMFOR_OP_ERROR);
				islpci_mgt_release(response);
			}
			if (r)
				printk(KERN_ERR "%s: mgt_commit_list: failure. "
					"oid=%08x err=%d\n",
					priv->ndev->name, oid, r);
			ret |= r;
			j++;
			oid++;
			data += t->size;
		}
	}
	return ret;
}

/* Lock outside */

void
mgt_set(islpci_private *priv, enum oid_num_t n, void *data)
{
	BUG_ON(n >= OID_NUM_LAST);
	BUG_ON(priv->mib[n] == NULL);

	memcpy(priv->mib[n], data, isl_oid[n].size);
	mgt_cpu_to_le(isl_oid[n].flags & OID_FLAG_TYPE, priv->mib[n]);
}

void
mgt_get(islpci_private *priv, enum oid_num_t n, void *res)
{
	BUG_ON(n >= OID_NUM_LAST);
	BUG_ON(priv->mib[n] == NULL);
	BUG_ON(res == NULL);

	memcpy(res, priv->mib[n], isl_oid[n].size);
	mgt_le_to_cpu(isl_oid[n].flags & OID_FLAG_TYPE, res);
}

/* Commits the cache. Lock outside. */

static enum oid_num_t commit_part1[] = {
	OID_INL_CONFIG,
	OID_INL_MODE,
	DOT11_OID_BSSTYPE,
	DOT11_OID_CHANNEL,
	DOT11_OID_MLMEAUTOLEVEL
};

static enum oid_num_t commit_part2[] = {
	DOT11_OID_SSID,
	DOT11_OID_PSMBUFFER,
	DOT11_OID_AUTHENABLE,
	DOT11_OID_PRIVACYINVOKED,
	DOT11_OID_EXUNENCRYPTED,
	DOT11_OID_DEFKEYX,	/* MULTIPLE */
	DOT11_OID_DEFKEYID,
	DOT11_OID_DOT1XENABLE,
	OID_INL_DOT11D_CONFORMANCE,
	/* Do not initialize this - fw < 1.0.4.3 rejects it
	OID_INL_OUTPUTPOWER,
	*/
};

/* update the MAC addr. */
static int
mgt_update_addr(islpci_private *priv)
{
	struct islpci_mgmtframe *res;
	int ret;

	ret = islpci_mgt_transaction(priv->ndev, PIMFOR_OP_GET,
				     isl_oid[GEN_OID_MACADDRESS].oid, NULL,
				     isl_oid[GEN_OID_MACADDRESS].size, &res);

	if ((ret == 0) && res && (res->header->operation != PIMFOR_OP_ERROR))
		memcpy(priv->ndev->dev_addr, res->data, ETH_ALEN);
	else
		ret = -EIO;
	if (res)
		islpci_mgt_release(res);

	if (ret)
		printk(KERN_ERR "%s: mgt_update_addr: failure\n", priv->ndev->name);
	return ret;
}

int
mgt_commit(islpci_private *priv)
{
	int rvalue;
	enum oid_num_t u;

	if (islpci_get_state(priv) < PRV_STATE_INIT)
		return 0;

	rvalue = mgt_commit_list(priv, commit_part1, ARRAY_SIZE(commit_part1));

	if (priv->iw_mode != IW_MODE_MONITOR)
		rvalue |= mgt_commit_list(priv, commit_part2, ARRAY_SIZE(commit_part2));

	u = OID_INL_MODE;
	rvalue |= mgt_commit_list(priv, &u, 1);
	rvalue |= mgt_update_addr(priv);

	if (rvalue) {
		/* some request have failed. The device might be in an
		   incoherent state. We should reset it ! */
		printk(KERN_DEBUG "%s: mgt_commit: failure\n", priv->ndev->name);
	}
	return rvalue;
}

/* The following OIDs need to be "unlatched":
 *
 * MEDIUMLIMIT,BEACONPERIOD,DTIMPERIOD,ATIMWINDOW,LISTENINTERVAL
 * FREQUENCY,EXTENDEDRATES.
 *
 * The way to do this is to set ESSID. Note though that they may get
 * unlatch before though by setting another OID. */
#if 0
void
mgt_unlatch_all(islpci_private *priv)
{
	u32 u;
	int rvalue = 0;

	if (islpci_get_state(priv) < PRV_STATE_INIT)
		return;

	u = DOT11_OID_SSID;
	rvalue = mgt_commit_list(priv, &u, 1);
	/* Necessary if in MANUAL RUN mode? */
#if 0
	u = OID_INL_MODE;
	rvalue |= mgt_commit_list(priv, &u, 1);

	u = DOT11_OID_MLMEAUTOLEVEL;
	rvalue |= mgt_commit_list(priv, &u, 1);

	u = OID_INL_MODE;
	rvalue |= mgt_commit_list(priv, &u, 1);
#endif

	if (rvalue)
		printk(KERN_DEBUG "%s: Unlatching OIDs failed\n", priv->ndev->name);
}
#endif

/* This will tell you if you are allowed to answer a mlme(ex) request .*/

int
mgt_mlme_answer(islpci_private *priv)
{
	u32 mlmeautolevel;
	/* Acquire a read lock because if we are in a mode change, it's
	 * possible to answer true, while the card is leaving master to managed
	 * mode. Answering to a mlme in this situation could hang the card.
	 */
	down_read(&priv->mib_sem);
	mlmeautolevel =
	    le32_to_cpu(*(u32 *) priv->mib[DOT11_OID_MLMEAUTOLEVEL]);
	up_read(&priv->mib_sem);

	return ((priv->iw_mode == IW_MODE_MASTER) &&
		(mlmeautolevel >= DOT11_MLME_INTERMEDIATE));
}

enum oid_num_t
mgt_oidtonum(u32 oid)
{
	int i;

	for (i = 0; i < OID_NUM_LAST; i++)
		if (isl_oid[i].oid == oid)
			return i;

	printk(KERN_DEBUG "looking for an unknown oid 0x%x", oid);

	return OID_NUM_LAST;
}

int
mgt_response_to_str(enum oid_num_t n, union oid_res_t *r, char *str)
{
	switch (isl_oid[n].flags & OID_FLAG_TYPE) {
	case OID_TYPE_U32:
		return snprintf(str, PRIV_STR_SIZE, "%u\n", r->u);
	case OID_TYPE_BUFFER:{
			struct obj_buffer *buff = r->ptr;
			return snprintf(str, PRIV_STR_SIZE,
					"size=%u\naddr=0x%X\n", buff->size,
					buff->addr);
		}
		break;
	case OID_TYPE_BSS:{
			struct obj_bss *bss = r->ptr;
			return snprintf(str, PRIV_STR_SIZE,
					"age=%u\nchannel=%u\n"
					"capinfo=0x%X\nrates=0x%X\n"
					"basic_rates=0x%X\n", bss->age,
					bss->channel, bss->capinfo,
					bss->rates, bss->basic_rates);
		}
		break;
	case OID_TYPE_BSSLIST:{
			struct obj_bsslist *list = r->ptr;
			int i, k;
			k = snprintf(str, PRIV_STR_SIZE, "nr=%u\n", list->nr);
			for (i = 0; i < list->nr; i++)
				k += snprintf(str + k, PRIV_STR_SIZE - k,
					      "bss[%u] :\nage=%u\nchannel=%u\n"
					      "capinfo=0x%X\nrates=0x%X\n"
					      "basic_rates=0x%X\n",
					      i, list->bsslist[i].age,
					      list->bsslist[i].channel,
					      list->bsslist[i].capinfo,
					      list->bsslist[i].rates,
					      list->bsslist[i].basic_rates);
			return k;
		}
		break;
	case OID_TYPE_FREQUENCIES:{
			struct obj_frequencies *freq = r->ptr;
			int i, t;
			printk("nr : %u\n", freq->nr);
			t = snprintf(str, PRIV_STR_SIZE, "nr=%u\n", freq->nr);
			for (i = 0; i < freq->nr; i++)
				t += snprintf(str + t, PRIV_STR_SIZE - t,
					      "mhz[%u]=%u\n", i, freq->mhz[i]);
			return t;
		}
		break;
	case OID_TYPE_MLME:{
			struct obj_mlme *mlme = r->ptr;
			return snprintf(str, PRIV_STR_SIZE,
					"id=0x%X\nstate=0x%X\ncode=0x%X\n",
					mlme->id, mlme->state, mlme->code);
		}
		break;
	case OID_TYPE_MLMEEX:{
			struct obj_mlmeex *mlme = r->ptr;
			return snprintf(str, PRIV_STR_SIZE,
					"id=0x%X\nstate=0x%X\n"
					"code=0x%X\nsize=0x%X\n", mlme->id,
					mlme->state, mlme->code, mlme->size);
		}
		break;
	case OID_TYPE_ATTACH:{
			struct obj_attachment *attach = r->ptr;
			return snprintf(str, PRIV_STR_SIZE,
					"id=%d\nsize=%d\n",
					attach->id,
					attach->size);
		}
		break;
	case OID_TYPE_SSID:{
			struct obj_ssid *ssid = r->ptr;
			return snprintf(str, PRIV_STR_SIZE,
					"length=%u\noctets=%.*s\n",
					ssid->length, ssid->length,
					ssid->octets);
		}
		break;
	case OID_TYPE_KEY:{
			struct obj_key *key = r->ptr;
			int t, i;
			t = snprintf(str, PRIV_STR_SIZE,
				     "type=0x%X\nlength=0x%X\nkey=0x",
				     key->type, key->length);
			for (i = 0; i < key->length; i++)
				t += snprintf(str + t, PRIV_STR_SIZE - t,
					      "%02X:", key->key[i]);
			t += snprintf(str + t, PRIV_STR_SIZE - t, "\n");
			return t;
		}
		break;
	case OID_TYPE_RAW:
	case OID_TYPE_ADDR:{
			unsigned char *buff = r->ptr;
			int t, i;
			t = snprintf(str, PRIV_STR_SIZE, "hex data=");
			for (i = 0; i < isl_oid[n].size; i++)
				t += snprintf(str + t, PRIV_STR_SIZE - t,
					      "%02X:", buff[i]);
			t += snprintf(str + t, PRIV_STR_SIZE - t, "\n");
			return t;
		}
		break;
	default:
		BUG();
	}
	return 0;
}

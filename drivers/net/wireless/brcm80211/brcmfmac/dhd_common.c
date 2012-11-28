/*
 * Copyright (c) 2010 Broadcom Corporation
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/netdevice.h>
#include <brcmu_wifi.h>
#include <brcmu_utils.h>
#include "dhd.h"
#include "dhd_bus.h"
#include "dhd_proto.h"
#include "dhd_dbg.h"
#include "fwil.h"

#define PKTFILTER_BUF_SIZE		128
#define BRCMF_ARPOL_MODE		0xb	/* agent|snoop|peer_autoreply */
#define BRCMF_DEFAULT_BCN_TIMEOUT	3
#define BRCMF_DEFAULT_SCAN_CHANNEL_TIME	40
#define BRCMF_DEFAULT_SCAN_UNASSOC_TIME	40
#define BRCMF_DEFAULT_PACKET_FILTER	"100 0 0 0 0x01 0x00"

#ifdef DEBUG
static const char brcmf_version[] =
	"Dongle Host Driver, version " BRCMF_VERSION_STR "\nCompiled on "
	__DATE__ " at " __TIME__;
#else
static const char brcmf_version[] =
	"Dongle Host Driver, version " BRCMF_VERSION_STR;
#endif


bool brcmf_c_prec_enq(struct device *dev, struct pktq *q,
		      struct sk_buff *pkt, int prec)
{
	struct sk_buff *p;
	int eprec = -1;		/* precedence to evict from */
	bool discard_oldest;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	/* Fast case, precedence queue is not full and we are also not
	 * exceeding total queue length
	 */
	if (!pktq_pfull(q, prec) && !pktq_full(q)) {
		brcmu_pktq_penq(q, prec, pkt);
		return true;
	}

	/* Determine precedence from which to evict packet, if any */
	if (pktq_pfull(q, prec))
		eprec = prec;
	else if (pktq_full(q)) {
		p = brcmu_pktq_peek_tail(q, &eprec);
		if (eprec > prec)
			return false;
	}

	/* Evict if needed */
	if (eprec >= 0) {
		/* Detect queueing to unconfigured precedence */
		discard_oldest = ac_bitmap_tst(drvr->wme_dp, eprec);
		if (eprec == prec && !discard_oldest)
			return false;	/* refuse newer (incoming) packet */
		/* Evict packet according to discard policy */
		p = discard_oldest ? brcmu_pktq_pdeq(q, eprec) :
			brcmu_pktq_pdeq_tail(q, eprec);
		if (p == NULL)
			brcmf_dbg(ERROR, "brcmu_pktq_penq() failed, oldest %d\n",
				  discard_oldest);

		brcmu_pkt_buf_free_skb(p);
	}

	/* Enqueue */
	p = brcmu_pktq_penq(q, prec, pkt);
	if (p == NULL)
		brcmf_dbg(ERROR, "brcmu_pktq_penq() failed\n");

	return p != NULL;
}

/* Convert user's input in hex pattern to byte-size mask */
static int brcmf_c_pattern_atoh(char *src, char *dst)
{
	int i;
	if (strncmp(src, "0x", 2) != 0 && strncmp(src, "0X", 2) != 0) {
		brcmf_dbg(ERROR, "Mask invalid format. Needs to start with 0x\n");
		return -EINVAL;
	}
	src = src + 2;		/* Skip past 0x */
	if (strlen(src) % 2 != 0) {
		brcmf_dbg(ERROR, "Mask invalid format. Length must be even.\n");
		return -EINVAL;
	}
	for (i = 0; *src != '\0'; i++) {
		unsigned long res;
		char num[3];
		strncpy(num, src, 2);
		num[2] = '\0';
		if (kstrtoul(num, 16, &res))
			return -EINVAL;
		dst[i] = (u8)res;
		src += 2;
	}
	return i;
}

static void
brcmf_c_pktfilter_offload_enable(struct brcmf_if *ifp, char *arg, int enable,
				 int master_mode)
{
	unsigned long res;
	char *argv;
	char *arg_save = NULL, *arg_org = NULL;
	s32 err;
	struct brcmf_pkt_filter_enable_le enable_parm;

	arg_save = kstrdup(arg, GFP_ATOMIC);
	if (!arg_save)
		goto fail;

	arg_org = arg_save;

	argv = strsep(&arg_save, " ");

	if (argv == NULL) {
		brcmf_dbg(ERROR, "No args provided\n");
		goto fail;
	}

	/* Parse packet filter id. */
	enable_parm.id = 0;
	if (!kstrtoul(argv, 0, &res))
		enable_parm.id = cpu_to_le32((u32)res);

	/* Enable/disable the specified filter. */
	enable_parm.enable = cpu_to_le32(enable);

	err = brcmf_fil_iovar_data_set(ifp, "pkt_filter_enable", &enable_parm,
				       sizeof(enable_parm));
	if (err)
		brcmf_dbg(ERROR, "Set pkt_filter_enable error (%d)\n", err);

	/* Control the master mode */
	err = brcmf_fil_iovar_int_set(ifp, "pkt_filter_mode", master_mode);
	if (err)
		brcmf_dbg(ERROR, "Set pkt_filter_mode error (%d)\n", err);

fail:
	kfree(arg_org);
}

static void brcmf_c_pktfilter_offload_set(struct brcmf_if *ifp, char *arg)
{
	struct brcmf_pkt_filter_le *pkt_filter;
	unsigned long res;
	int buf_len;
	s32 err;
	u32 mask_size;
	u32 pattern_size;
	char *argv[8], *buf = NULL;
	int i = 0;
	char *arg_save = NULL, *arg_org = NULL;

	arg_save = kstrdup(arg, GFP_ATOMIC);
	if (!arg_save)
		goto fail;

	arg_org = arg_save;

	buf = kmalloc(PKTFILTER_BUF_SIZE, GFP_ATOMIC);
	if (!buf)
		goto fail;

	argv[i] = strsep(&arg_save, " ");
	while (argv[i]) {
		i++;
		if (i >= 8) {
			brcmf_dbg(ERROR, "Too many parameters\n");
			goto fail;
		}
		argv[i] = strsep(&arg_save, " ");
	}

	if (i != 6) {
		brcmf_dbg(ERROR, "Not enough args provided %d\n", i);
		goto fail;
	}

	pkt_filter = (struct brcmf_pkt_filter_le *)buf;

	/* Parse packet filter id. */
	pkt_filter->id = 0;
	if (!kstrtoul(argv[0], 0, &res))
		pkt_filter->id = cpu_to_le32((u32)res);

	/* Parse filter polarity. */
	pkt_filter->negate_match = 0;
	if (!kstrtoul(argv[1], 0, &res))
		pkt_filter->negate_match = cpu_to_le32((u32)res);

	/* Parse filter type. */
	pkt_filter->type = 0;
	if (!kstrtoul(argv[2], 0, &res))
		pkt_filter->type = cpu_to_le32((u32)res);

	/* Parse pattern filter offset. */
	pkt_filter->u.pattern.offset = 0;
	if (!kstrtoul(argv[3], 0, &res))
		pkt_filter->u.pattern.offset = cpu_to_le32((u32)res);

	/* Parse pattern filter mask. */
	mask_size = brcmf_c_pattern_atoh(argv[4],
			(char *)pkt_filter->u.pattern.mask_and_pattern);

	/* Parse pattern filter pattern. */
	pattern_size = brcmf_c_pattern_atoh(argv[5],
		(char *)&pkt_filter->u.pattern.mask_and_pattern[mask_size]);

	if (mask_size != pattern_size) {
		brcmf_dbg(ERROR, "Mask and pattern not the same size\n");
		goto fail;
	}

	pkt_filter->u.pattern.size_bytes = cpu_to_le32(mask_size);
	buf_len = offsetof(struct brcmf_pkt_filter_le,
			   u.pattern.mask_and_pattern);
	buf_len += mask_size + pattern_size;

	err = brcmf_fil_iovar_data_set(ifp, "pkt_filter_add", pkt_filter,
				       buf_len);
	if (err)
		brcmf_dbg(ERROR, "Set pkt_filter_add error (%d)\n", err);

fail:
	kfree(arg_org);

	kfree(buf);
}

int brcmf_c_preinit_dcmds(struct brcmf_if *ifp)
{
	s8 eventmask[BRCMF_EVENTING_MASK_LEN];
	u8 buf[BRCMF_DCMD_SMLEN];
	char *ptr;
	s32 err;
	struct brcmf_bus_dcmd *cmdlst;
	struct list_head *cur, *q;

	/* retreive mac address */
	err = brcmf_fil_iovar_data_get(ifp, "cur_etheraddr", ifp->mac_addr,
				       sizeof(ifp->mac_addr));
	if (err < 0) {
		brcmf_dbg(ERROR, "Retreiving cur_etheraddr failed, %d\n",
			  err);
		goto done;
	}
	memcpy(ifp->drvr->mac, ifp->mac_addr, sizeof(ifp->drvr->mac));

	/* query for 'ver' to get version info from firmware */
	memset(buf, 0, sizeof(buf));
	strcpy(buf, "ver");
	err = brcmf_fil_iovar_data_get(ifp, "ver", buf, sizeof(buf));
	if (err < 0) {
		brcmf_dbg(ERROR, "Retreiving version information failed, %d\n",
			  err);
		goto done;
	}
	ptr = (char *)buf;
	strsep(&ptr, "\n");
	/* Print fw version info */
	brcmf_dbg(ERROR, "Firmware version = %s\n", buf);

	/*
	 * Setup timeout if Beacons are lost and roam is off to report
	 * link down
	 */
	err = brcmf_fil_iovar_int_set(ifp, "bcn_timeout",
				      BRCMF_DEFAULT_BCN_TIMEOUT);
	if (err) {
		brcmf_dbg(ERROR, "bcn_timeout error (%d)\n", err);
		goto done;
	}

	/* Enable/Disable build-in roaming to allowed ext supplicant to take
	 * of romaing
	 */
	err = brcmf_fil_iovar_int_set(ifp, "roam_off", 1);
	if (err) {
		brcmf_dbg(ERROR, "roam_off error (%d)\n", err);
		goto done;
	}

	/* Setup event_msgs, enable E_IF */
	err = brcmf_fil_iovar_data_get(ifp, "event_msgs", eventmask,
				       BRCMF_EVENTING_MASK_LEN);
	if (err) {
		brcmf_dbg(ERROR, "Get event_msgs error (%d)\n", err);
		goto done;
	}
	setbit(eventmask, BRCMF_E_IF);
	err = brcmf_fil_iovar_data_set(ifp, "event_msgs", eventmask,
				       BRCMF_EVENTING_MASK_LEN);
	if (err) {
		brcmf_dbg(ERROR, "Set event_msgs error (%d)\n", err);
		goto done;
	}

	/* Setup default scan channel time */
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_CHANNEL_TIME,
				    BRCMF_DEFAULT_SCAN_CHANNEL_TIME);
	if (err) {
		brcmf_dbg(ERROR, "BRCMF_C_SET_SCAN_CHANNEL_TIME error (%d)\n",
			  err);
		goto done;
	}

	/* Setup default scan unassoc time */
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_UNASSOC_TIME,
				    BRCMF_DEFAULT_SCAN_UNASSOC_TIME);
	if (err) {
		brcmf_dbg(ERROR, "BRCMF_C_SET_SCAN_UNASSOC_TIME error (%d)\n",
			  err);
		goto done;
	}

	/* Try to set and enable ARP offload feature, this may fail */
	err = brcmf_fil_iovar_int_set(ifp, "arp_ol", BRCMF_ARPOL_MODE);
	if (err) {
		brcmf_dbg(TRACE, "failed to set ARP offload mode to 0x%x, err = %d\n",
			  BRCMF_ARPOL_MODE, err);
		err = 0;
	} else {
		err = brcmf_fil_iovar_int_set(ifp, "arpoe", 1);
		if (err) {
			brcmf_dbg(TRACE, "failed to enable ARP offload err = %d\n",
				  err);
			err = 0;
		} else
			brcmf_dbg(TRACE, "successfully enabled ARP offload to 0x%x\n",
				  BRCMF_ARPOL_MODE);
	}

	/* Setup packet filter */
	brcmf_c_pktfilter_offload_set(ifp, BRCMF_DEFAULT_PACKET_FILTER);
	brcmf_c_pktfilter_offload_enable(ifp, BRCMF_DEFAULT_PACKET_FILTER,
					 0, true);

	/* set bus specific command if there is any */
	list_for_each_safe(cur, q, &ifp->drvr->bus_if->dcmd_list) {
		cmdlst = list_entry(cur, struct brcmf_bus_dcmd, list);
		if (cmdlst->name && cmdlst->param && cmdlst->param_len) {
			brcmf_fil_iovar_data_set(ifp, cmdlst->name,
						 cmdlst->param,
						 cmdlst->param_len);
		}
		list_del(cur);
		kfree(cmdlst);
	}
done:
	return err;
}

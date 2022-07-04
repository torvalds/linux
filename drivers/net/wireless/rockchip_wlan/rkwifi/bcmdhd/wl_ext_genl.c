#ifdef WL_EXT_GENL
#include <bcmendian.h>
#include <wl_android.h>
#include <dhd_config.h>
#include <net/genetlink.h>

#define AGENL_ERROR(name, arg1, args...) \
	do { \
		if (android_msg_level & ANDROID_ERROR_LEVEL) { \
			printf("[%s] AGENL-ERROR) %s : " arg1, name, __func__, ## args); \
		} \
	} while (0)
#define AGENL_TRACE(name, arg1, args...) \
	do { \
		if (android_msg_level & ANDROID_TRACE_LEVEL) { \
			printf("[%s] AGENL-TRACE) %s : " arg1, name, __func__, ## args); \
		} \
	} while (0)
#define AGENL_INFO(name, arg1, args...) \
	do { \
		if (android_msg_level & ANDROID_INFO_LEVEL) { \
			printf("[%s] AGENL-INFO) %s : " arg1, name, __func__, ## args); \
		} \
	} while (0)

#define htod32(i) i
#define htod16(i) i
#define dtoh32(i) i
#define dtoh16(i) i

#ifdef SENDPROB
#define MGMT_PROBE_REQ 0x40
#define MGMT_PROBE_RES 0x50
#endif

enum {
	__GENL_CUSTOM_ATTR_INVALID,
	GENL_CUSTOM_ATTR_MSG,	/* message */
	__GENL_CUSTOM_ATTR_MAX,
};

enum {
	__GENLL_CUSTOM_COMMAND_INVALID,
	GENL_CUSTOM_COMMAND_BIND,	/* bind */
	GENL_CUSTOM_COMMAND_SEND,	/* user -> kernel */
	GENL_CUSTOM_COMMAND_RECV,	/* kernel -> user */
	__GENL_CUSTOM_COMMAND_MAX,
};

#if defined(ALIBABA_ZEROCONFIG)
#define GENL_FAMILY_NAME	"WIFI_NL_CUSTOM"
#define PROBE_RSP_DST_MAC_OFFSET	4
#define PROBE_RSP_VNDR_ID_OFFSET	55
#else
#define GENL_FAMILY_NAME	"WLAN_NL_CUSTOM"
#define PROBE_RSP_DST_MAC_OFFSET	4
#define PROBE_RSP_VNDR_ID_OFFSET	DOT11_MGMT_HDR_LEN
#endif
#define PROBE_RSP_VNDR_LEN_OFFSET	(PROBE_RSP_VNDR_ID_OFFSET+1)
#define PROBE_RSP_VNDR_OUI_OFFSET	(PROBE_RSP_VNDR_ID_OFFSET+2)
#define MAX_CUSTOM_PKT_LENGTH	2048
#define GENL_CUSTOM_ATTR_MAX	(__GENL_CUSTOM_ATTR_MAX - 1)
#define GENLMSG_UNICAST_RETRY_LIMIT 5

typedef struct genl_params {
	struct net_device *dev;
	bool bind;
	int pm;
	int bind_pid;
	int send_retry_cnt;
} genl_params_t;

struct genl_params *g_zconf = NULL;

static int wl_ext_genl_bind(struct sk_buff *skb, struct genl_info *info);
static int wl_ext_genl_recv(struct sk_buff *skb, struct genl_info *info);
static int wl_ext_genl_send(struct genl_params *zconf, struct net_device *dev,
	char* buf, int buf_len);

static struct nla_policy wl_ext_genl_policy[GENL_CUSTOM_ATTR_MAX + 1] = {
	[GENL_CUSTOM_ATTR_MSG] = {.type = NLA_NUL_STRING},
};

static struct genl_ops wl_ext_genl_ops[] = {
	{
		.cmd = GENL_CUSTOM_COMMAND_BIND,
		.flags = 0,
		.policy = wl_ext_genl_policy,
		.doit = wl_ext_genl_bind,
		.dumpit = NULL,
	},
	{
		.cmd = GENL_CUSTOM_COMMAND_SEND,
		.flags = 0,
		.policy = wl_ext_genl_policy,
		.doit = wl_ext_genl_recv,
		.dumpit = NULL,
	},
};

static struct genl_family wl_ext_genl_family = {
	.id = GENL_ID_GENERATE,
	.hdrsize = 0,
	.name = GENL_FAMILY_NAME,
	.version = 1,
	.maxattr = GENL_CUSTOM_ATTR_MAX,
};

#ifdef SENDPROB
static int
wl_ext_add_del_ie_hex(struct net_device *dev, uint pktflag,
	char *ie_data, int ie_len, const char* add_del_cmd)
{
	vndr_ie_setbuf_t *vndr_ie = NULL;
	char iovar_buf[WLC_IOCTL_SMLEN]="\0";
	int tot_len = 0, iecount;
	int err = -1;

	if (!ie_len) {
		AGENL_ERROR(dev->name, "wrong ie_len %d\n", ie_len);
		goto exit;
	}

	tot_len = (int)(sizeof(vndr_ie_setbuf_t) + (ie_len));
	vndr_ie = (vndr_ie_setbuf_t *) kzalloc(tot_len, GFP_KERNEL);
	if (!vndr_ie) {
		AGENL_ERROR(dev->name, "IE memory alloc failed\n");
		err = -ENOMEM;
		goto exit;
	}

	/* Copy the vndr_ie SET command ("add"/"del") to the buffer */
	strncpy(vndr_ie->cmd, add_del_cmd, VNDR_IE_CMD_LEN - 1);
	vndr_ie->cmd[VNDR_IE_CMD_LEN - 1] = '\0';

	/* Set the IE count - the buffer contains only 1 IE */
	iecount = htod32(1);
	memcpy((void *)&vndr_ie->vndr_ie_buffer.iecount, &iecount, sizeof(s32));

	/* Set packet flag to indicate that BEACON's will contain this IE */
	pktflag = htod32(pktflag);
	memcpy((void *)&vndr_ie->vndr_ie_buffer.vndr_ie_list[0].pktflag, &pktflag,
		sizeof(u32));

	/* Set the IE ID */
	vndr_ie->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.id = (uchar)DOT11_MNG_VS_ID;

	/* Set the IE LEN */
	vndr_ie->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.len = ie_len;

	/* Set the IE OUI and DATA */
	memcpy((char *)vndr_ie->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.oui, ie_data, ie_len);

	err = wldev_iovar_setbuf(dev, "vndr_ie", vndr_ie, tot_len,
		iovar_buf, sizeof(iovar_buf), NULL);
	if (err != 0)
		AGENL_ERROR(dev->name, "vndr_ie, ret=%d\n", err);

exit:
	if (vndr_ie) {
		kfree(vndr_ie);
	}
	return err;
}

static int
wl_ext_send_probersp(struct net_device *dev, char* buf, int buf_len)
{
	char addr[ETHER_ADDR_LEN], *pVndrOUI;
	char iovar_buf[WLC_IOCTL_SMLEN]="\0";
	int err = -1, ie_len;

	if (buf == NULL || buf_len <= 0){
		AGENL_ERROR(dev->name, "buf is NULL or buf_len <= 0\n");
		return -1;
	}

	AGENL_TRACE(dev->name, "Enter\n");

	memcpy(addr, (buf+PROBE_RSP_DST_MAC_OFFSET), ETHER_ADDR_LEN);
	pVndrOUI = (buf+PROBE_RSP_VNDR_OUI_OFFSET);
	ie_len = *(buf+PROBE_RSP_VNDR_LEN_OFFSET);

	if (ie_len > (buf_len-PROBE_RSP_VNDR_OUI_OFFSET)) {
		AGENL_ERROR(dev->name, "wrong vendor ie len %d\n", ie_len);
		return -1;
	}

	err = wl_ext_add_del_ie_hex(dev, VNDR_IE_PRBRSP_FLAG, pVndrOUI, ie_len, "add");
	if (err)
		goto exit;

	err = wldev_iovar_setbuf(dev, "send_probresp", addr, ETHER_ADDR_LEN,
		iovar_buf, sizeof(iovar_buf), NULL);
	if (err != 0)
		AGENL_ERROR(dev->name, "vndr_ie, ret=%d\n", err);

	OSL_SLEEP(100);
	wl_ext_add_del_ie_hex(dev, VNDR_IE_PRBRSP_FLAG, pVndrOUI, ie_len, "del");

exit:
	return err;
}

static int
wl_ext_set_probreq(struct net_device *dev, bool set)
{
	int bytes_written = 0;
	char recv_probreq[32];

	AGENL_TRACE(dev->name, "Enter\n");

	if (set) {
		sprintf(recv_probreq, "wl recv_probreq 1");
		wl_android_ext_priv_cmd(dev, recv_probreq, 0, &bytes_written);
	} else {
		sprintf(recv_probreq, "wl recv_probreq 0");
		wl_android_ext_priv_cmd(dev, recv_probreq, 0, &bytes_written);
	}

	return 0;
}

void
wl_ext_probreq_event(struct net_device *dev, void *argu,
	const wl_event_msg_t *e, void *data)
{
	struct genl_params *zconf = (struct genl_params *)argu;
	int i, ret = 0, num_ie = 0, totlen;
	uint32 event_len = 0;
	char *buf, *pbuf;
	uint rem_len, buflen = MAX_CUSTOM_PKT_LENGTH;
	uint32 event_id[] = {DOT11_MNG_VS_ID};
	uint32 datalen = ntoh32(e->datalen);
	bcm_tlv_t *ie;

	AGENL_TRACE(dev->name, "Enter\n");

	rem_len = buflen;
	buf = kzalloc(MAX_CUSTOM_PKT_LENGTH, GFP_KERNEL);
	if (unlikely(!buf)) {
		AGENL_ERROR(dev->name, "Could not allocate buf\n");
		return;
	}

	// copy mgmt header
	pbuf = buf;
	memcpy(pbuf, data, DOT11_MGMT_HDR_LEN);
	rem_len -= (DOT11_MGMT_HDR_LEN+1);
	datalen -= DOT11_MGMT_HDR_LEN;
	data += DOT11_MGMT_HDR_LEN;

	// copy IEs
	pbuf = buf + DOT11_MGMT_HDR_LEN;
#if 1 // non-sort by id
	ie = (bcm_tlv_t*)data;
	totlen = datalen;
	while (ie && totlen >= TLV_HDR_LEN) {
		int ie_id = -1;
		int ie_len = ie->len + TLV_HDR_LEN;
		for (i=0; i<sizeof(event_id)/sizeof(event_id[0]); i++) {
			if (ie->id == event_id[i]) {
				ie_id = ie->id;
				break;
			}
		}
		if ((ie->id == ie_id) && (totlen >= ie_len) && (rem_len >= ie_len)) {
			memcpy(pbuf, ie, ie_len);
			pbuf += ie_len;
			rem_len -= ie_len;
			num_ie++;
		}
		ie = (bcm_tlv_t*)((uint8*)ie + ie_len);
		totlen -= ie_len;
	}
#else // sort by id
	for (i = 0; i < sizeof(event_id)/sizeof(event_id[0]); i++) {
		void *pdata = data;
		int data_len = datalen;
		while (rem_len > 0) {
			ie = bcm_parse_tlvs(pdata, data_len, event_id[i]);
			if (!ie)
				break;
			if (rem_len < (ie->len+TLV_HDR_LEN)) {
				ANDROID_TRACE(("%s: buffer is not enough\n", __FUNCTION__));
				break;
			}
			memcpy(pbuf, ie, min(ie->len+TLV_HDR_LEN, rem_len));
			pbuf += (ie->len+TLV_HDR_LEN);
			rem_len -= (ie->len+TLV_HDR_LEN);
			data_len -= (((void *)ie-pdata) + (ie->len+TLV_HDR_LEN));
			pdata = (char *)ie + (ie->len+TLV_HDR_LEN);
			num_ie++;
		}
	}
#endif
	if (num_ie) {
		event_len = buflen - rem_len;
		AGENL_INFO(dev->name, "num_ie=%d\n", num_ie);
		if (android_msg_level & ANDROID_INFO_LEVEL)
			prhex("buf", buf, event_len);
		ret = wl_ext_genl_send(zconf, dev, buf, event_len);
	}

	if(buf)
		kfree(buf);
	return;
}
#endif

static int
wl_ext_genl_recv(struct sk_buff *skb, struct genl_info *info)
{
	struct genl_params *zconf = g_zconf;
	struct net_device *dev;
	struct nlattr *na;
	char* pData = NULL;
	int DataLen = 0;

	if (info == NULL) {
		AGENL_ERROR(dev->name, "genl_info is NULL\n");
		return -1;
	}

	if (zconf == NULL) {
		AGENL_ERROR("wlan", "g_zconf is NULL\n");
		return -1;
	}
	dev = zconf->dev;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
	AGENL_TRACE(dev->name, "Enter snd_portid=%d\n", info->snd_portid);
#else
	AGENL_TRACE(dev->name, "Enter\n");
#endif
	na = info->attrs[GENL_CUSTOM_ATTR_MSG];

	if (na) {
		pData = (char*) nla_data(na);
		DataLen = nla_len(na);
		AGENL_INFO(dev->name, "nla_len(na) : %d\n", DataLen);
		if (android_msg_level & ANDROID_INFO_LEVEL)
			prhex("nla_data(na)", pData, DataLen);
	}

#ifdef SENDPROB
	if(*pData == MGMT_PROBE_RES) {
		wl_ext_send_probersp(dev, pData, DataLen);
	} else if(*pData == MGMT_PROBE_REQ) {
		AGENL_ERROR(dev->name, "probe req\n");
	} else {
		AGENL_ERROR(dev->name, "Unexpected pkt %d\n", *pData);
		if (android_msg_level & ANDROID_INFO_LEVEL)
			prhex("nla_data(na)", pData, DataLen);
	}
#endif

	return 0;
}

static int
wl_ext_genl_send(struct genl_params *zconf, struct net_device *dev,
	char* buf, int buf_len)
{
	struct sk_buff *skb = NULL;
	char* msg_head = NULL;
	int ret = -1;
	int bytes_written = 0;
	char recv_probreq[32];

	if (zconf->bind_pid == -1) {
		AGENL_ERROR(dev->name, "There is no binded process\n");
		return -1;
	}

	if(buf == NULL || buf_len <= 0) {
		AGENL_ERROR(dev->name, "buf is NULL or buf_len : %d\n", buf_len);
		return -1;
	}

	skb = genlmsg_new(MAX_CUSTOM_PKT_LENGTH, GFP_KERNEL);

	if (skb) {
		msg_head = genlmsg_put(skb, 0, 0, &wl_ext_genl_family, 0, GENL_CUSTOM_COMMAND_RECV);
		if (msg_head == NULL) {
			nlmsg_free(skb);
			AGENL_ERROR(dev->name, "genlmsg_put fail\n");
			return -1;
		}

		ret = nla_put(skb, GENL_CUSTOM_ATTR_MSG, buf_len, buf);
		if (ret != 0) {
			nlmsg_free(skb);
			AGENL_ERROR(dev->name, "nla_put fail : %d\n", ret);
			return ret;
		}

		genlmsg_end(skb, msg_head);

		/* sending message */
		AGENL_TRACE(dev->name, "send to process %d\n", zconf->bind_pid);
		ret = genlmsg_unicast(&init_net, skb, zconf->bind_pid);
		if (ret != 0) {
			AGENL_ERROR(dev->name, "genlmsg_unicast fail : %d\n", ret);
			zconf->send_retry_cnt++;
			if(zconf->send_retry_cnt >= GENLMSG_UNICAST_RETRY_LIMIT) {
				AGENL_ERROR(dev->name, "Exceeding retry cnt %d, Unbind pid : %d\n",
					zconf->send_retry_cnt, zconf->bind_pid);
				zconf->bind_pid = -1;
				sprintf(recv_probreq, "wl recv_probreq 0");
				wl_android_ext_priv_cmd(dev, recv_probreq, 0, &bytes_written);
			}
			return ret;
		}
	} else {
		AGENL_ERROR(dev->name, "genlmsg_new fail\n");
		return -1;
	}

	zconf->send_retry_cnt = 0;

	return 0;
}

static int
wl_ext_genl_bind(struct sk_buff *skb, struct genl_info *info)
{
	struct genl_params *zconf = g_zconf;
	struct net_device *dev;
	struct dhd_pub *dhd;
	struct nlattr *na;
	bool bind;
	char* pData = NULL;
	int DataLen = 0;

	if (info == NULL) {
		AGENL_ERROR("wlan", "genl_info is NULL\n");
		return -1;
	}

	if (zconf == NULL) {
		AGENL_ERROR("wlan", "zconf is NULL\n");
		return -1;
	}
	dev = zconf->dev;
	dhd = dhd_get_pub(dev);

	AGENL_TRACE(dev->name, "Enter\n");

	na = info->attrs[GENL_CUSTOM_ATTR_MSG];
	if (na) {
		pData = (char*) nla_data(na);
		DataLen = nla_len(na);
		AGENL_INFO(dev->name, "nla_len(na) : %d\n", DataLen);
		if (android_msg_level & ANDROID_INFO_LEVEL)
			prhex("nla_data(na)", pData, DataLen);
	}

	if (strcmp(pData, "BIND") == 0) {
		bind = TRUE;
	} else if (strcmp(pData, "UNBIND") == 0) {
		bind = FALSE;
	} else {
		AGENL_ERROR(dev->name, "Unknown cmd %s\n", pData);
		return -1;
	}

	if (bind == zconf->bind) {
		AGENL_TRACE(dev->name, "Already %s\n", bind?"BIND":"UNBIND");
		return 0;
	}

	if (bind) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
		zconf->bind_pid = info->snd_portid;
#endif
		AGENL_TRACE(dev->name, "BIND pid = %d\n", zconf->bind_pid);
#ifdef SENDPROB
		wl_ext_set_probreq(dev, TRUE);
#endif
		zconf->bind = TRUE;
		zconf->pm = dhd->conf->pm;
		dhd->conf->pm = PM_OFF;
	} else {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
		AGENL_TRACE(dev->name, "UNBIND snd_portid = %d\n", info->snd_portid);
#else
		AGENL_TRACE(dev->name, "UNBIND pid = %d\n", zconf->bind_pid);
#endif
		zconf->bind_pid = -1;
#ifdef SENDPROB
		wl_ext_set_probreq(dev, FALSE);
#endif
		dhd->conf->pm = zconf->pm;
		zconf->bind = FALSE;
	}

	return 0;
}

int
wl_ext_genl_init(struct net_device *net)
{
	struct dhd_pub *dhd = dhd_get_pub(net);
	struct genl_params *zconf = dhd->zconf;
	int ret = 0;

	AGENL_TRACE(net->name, "Enter falimy name: \"%s\"\n", wl_ext_genl_family.name);

	zconf = kzalloc(sizeof(struct genl_params), GFP_KERNEL);
	if (unlikely(!zconf)) {
		AGENL_ERROR(net->name, "Could not allocate zconf\n");
		return -ENOMEM;
	}
	dhd->zconf = (void *)zconf;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
	ret = genl_register_family(&wl_ext_genl_family);
	//fix me: how to attach wl_ext_genl_ops
	ret = -1;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
	ret = genl_register_family_with_ops(&wl_ext_genl_family, wl_ext_genl_ops);
#else
	ret = genl_register_family_with_ops(&wl_ext_genl_family, wl_ext_genl_ops,
		ARRAY_SIZE(wl_ext_genl_ops));
#endif
	if (ret != 0) {
		AGENL_ERROR(net->name, "GE_NELINK family registration fail\n");
		goto err;
	}
	zconf->bind_pid = -1;
#ifdef SENDPROB
	ret = wl_ext_event_register(net, dhd, WLC_E_PROBREQ_MSG, wl_ext_probreq_event,
		zconf, PRIO_EVENT_IAPSTA);
	if (ret)
		goto err;
#endif
	zconf->dev = net;
	g_zconf = zconf;

	return ret;
err:
	if(zconf)
		kfree(zconf);
	return ret;
}

void
wl_ext_genl_deinit(struct net_device *net)
{
	struct dhd_pub *dhd = dhd_get_pub(net);
	struct genl_params *zconf = dhd->zconf;

	AGENL_TRACE(net->name, "Enter\n");

#ifdef SENDPROB
	wl_ext_event_deregister(net, dhd, WLC_E_PROBREQ_MSG, wl_ext_probreq_event);
#endif

	genl_unregister_family(&wl_ext_genl_family);
	if(zconf != NULL) {
		kfree(dhd->zconf);
		dhd->zconf = NULL;
	}
	g_zconf = NULL;

}
#endif


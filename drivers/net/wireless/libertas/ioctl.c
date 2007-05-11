/**
  * This file contains ioctl functions
  */

#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>

#include <net/iw_handler.h>
#include <net/ieee80211.h>

#include "host.h"
#include "radiotap.h"
#include "decl.h"
#include "defs.h"
#include "dev.h"
#include "join.h"
#include "wext.h"

#define MAX_SCAN_CELL_SIZE      (IW_EV_ADDR_LEN + \
				IW_ESSID_MAX_SIZE + \
				IW_EV_UINT_LEN + IW_EV_FREQ_LEN + \
				IW_EV_QUAL_LEN + IW_ESSID_MAX_SIZE + \
				IW_EV_PARAM_LEN + 40)	/* 40 for WPAIE */

#define WAIT_FOR_SCAN_RRESULT_MAX_TIME (10 * HZ)

static int wlan_set_region(wlan_private * priv, u16 region_code)
{
	int i;

	for (i = 0; i < MRVDRV_MAX_REGION_CODE; i++) {
		// use the region code to search for the index
		if (region_code == libertas_region_code_to_index[i]) {
			priv->adapter->regiontableindex = (u16) i;
			priv->adapter->regioncode = region_code;
			break;
		}
	}

	// if it's unidentified region code
	if (i >= MRVDRV_MAX_REGION_CODE) {
		lbs_pr_debug(1, "region Code not identified\n");
		LEAVE();
		return -1;
	}

	if (libertas_set_regiontable(priv, priv->adapter->regioncode, 0)) {
		LEAVE();
		return -EINVAL;
	}

	return 0;
}

static inline int hex2int(char c)
{
	if (c >= '0' && c <= '9')
		return (c - '0');
	if (c >= 'a' && c <= 'f')
		return (c - 'a' + 10);
	if (c >= 'A' && c <= 'F')
		return (c - 'A' + 10);
	return -1;
}

/* Convert a string representation of a MAC address ("xx:xx:xx:xx:xx:xx")
   into binary format (6 bytes).

   This function expects that each byte is represented with 2 characters
   (e.g., 11:2:11:11:11:11 is invalid)

 */
static char *eth_str2addr(char *ethstr, u8 * addr)
{
	int i, val, val2;
	char *pos = ethstr;

	/* get rid of initial blanks */
	while (*pos == ' ' || *pos == '\t')
		++pos;

	for (i = 0; i < 6; i++) {
		val = hex2int(*pos++);
		if (val < 0)
			return NULL;
		val2 = hex2int(*pos++);
		if (val2 < 0)
			return NULL;
		addr[i] = (val * 16 + val2) & 0xff;

		if (i < 5 && *pos++ != ':')
			return NULL;
	}
	return pos;
}

/* this writes xx:xx:xx:xx:xx:xx into ethstr
   (ethstr must have space for 18 chars) */
static int eth_addr2str(u8 * addr, char *ethstr)
{
	int i;
	char *pos = ethstr;

	for (i = 0; i < 6; i++) {
		sprintf(pos, "%02x", addr[i] & 0xff);
		pos += 2;
		if (i < 5)
			*pos++ = ':';
	}
	return 17;
}

/**
 *  @brief          Add an entry to the BT table
 *  @param priv     A pointer to wlan_private structure
 *  @param req      A pointer to ifreq structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_bt_add_ioctl(wlan_private * priv, struct ifreq *req)
{
	struct iwreq *wrq = (struct iwreq *)req;
	char ethaddrs_str[18];
	char *pos;
	u8 ethaddr[ETH_ALEN];

	ENTER();
	if (copy_from_user(ethaddrs_str, wrq->u.data.pointer,
			   sizeof(ethaddrs_str)))
		return -EFAULT;

	if ((pos = eth_str2addr(ethaddrs_str, ethaddr)) == NULL) {
		lbs_pr_info("BT_ADD: Invalid MAC address\n");
		return -EINVAL;
	}

	lbs_pr_debug(1, "BT: adding %s\n", ethaddrs_str);
	LEAVE();
	return (libertas_prepare_and_send_command(priv, cmd_bt_access,
				      cmd_act_bt_access_add,
				      cmd_option_waitforrsp, 0, ethaddr));
}

/**
 *  @brief          Delete an entry from the BT table
 *  @param priv     A pointer to wlan_private structure
 *  @param req      A pointer to ifreq structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_bt_del_ioctl(wlan_private * priv, struct ifreq *req)
{
	struct iwreq *wrq = (struct iwreq *)req;
	char ethaddrs_str[18];
	u8 ethaddr[ETH_ALEN];
	char *pos;

	ENTER();
	if (copy_from_user(ethaddrs_str, wrq->u.data.pointer,
			   sizeof(ethaddrs_str)))
		return -EFAULT;

	if ((pos = eth_str2addr(ethaddrs_str, ethaddr)) == NULL) {
		lbs_pr_info("Invalid MAC address\n");
		return -EINVAL;
	}

	lbs_pr_debug(1, "BT: deleting %s\n", ethaddrs_str);

	return (libertas_prepare_and_send_command(priv,
				      cmd_bt_access,
				      cmd_act_bt_access_del,
				      cmd_option_waitforrsp, 0, ethaddr));
	LEAVE();
	return 0;
}

/**
 *  @brief          Reset all entries from the BT table
 *  @param priv     A pointer to wlan_private structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_bt_reset_ioctl(wlan_private * priv)
{
	ENTER();

	lbs_pr_alert( "BT: resetting\n");

	return (libertas_prepare_and_send_command(priv,
				      cmd_bt_access,
				      cmd_act_bt_access_reset,
				      cmd_option_waitforrsp, 0, NULL));

	LEAVE();
	return 0;
}

/**
 *  @brief          List an entry from the BT table
 *  @param priv     A pointer to wlan_private structure
 *  @param req      A pointer to ifreq structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_bt_list_ioctl(wlan_private * priv, struct ifreq *req)
{
	int pos;
	char *addr1;
	struct iwreq *wrq = (struct iwreq *)req;
	/* used to pass id and store the bt entry returned by the FW */
	union {
		int id;
		char addr1addr2[2 * ETH_ALEN];
	} param;
	static char outstr[64];
	char *pbuf = outstr;
	int ret;

	ENTER();

	if (copy_from_user(outstr, wrq->u.data.pointer, sizeof(outstr))) {
		lbs_pr_debug(1, "Copy from user failed\n");
		return -1;
	}
	param.id = simple_strtoul(outstr, NULL, 10);
	pos = sprintf(pbuf, "%d: ", param.id);
	pbuf += pos;

	ret = libertas_prepare_and_send_command(priv, cmd_bt_access,
				    cmd_act_bt_access_list,
				    cmd_option_waitforrsp, 0,
				    (char *)&param);

	if (ret == 0) {
		addr1 = param.addr1addr2;

		pos = sprintf(pbuf, "ignoring traffic from ");
		pbuf += pos;
		pos = eth_addr2str(addr1, pbuf);
		pbuf += pos;
	} else {
		sprintf(pbuf, "(null)");
		pbuf += pos;
	}

	wrq->u.data.length = strlen(outstr);
	if (copy_to_user(wrq->u.data.pointer, (char *)outstr,
			 wrq->u.data.length)) {
		lbs_pr_debug(1, "BT_LIST: Copy to user failed!\n");
		return -EFAULT;
	}

	LEAVE();
	return 0;
}

/**
 *  @brief          Find the next parameter in an input string
 *  @param ptr      A pointer to the input parameter string
 *  @return         A pointer to the next parameter, or 0 if no parameters left.
 */
static char * next_param(char * ptr)
{
	if (!ptr) return NULL;
	while (*ptr == ' ' || *ptr == '\t') ++ptr;
	return (*ptr == '\0') ? NULL : ptr;
}

/**
 *  @brief          Add an entry to the FWT table
 *  @param priv     A pointer to wlan_private structure
 *  @param req      A pointer to ifreq structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_fwt_add_ioctl(wlan_private * priv, struct ifreq *req)
{
	struct iwreq *wrq = (struct iwreq *)req;
	char in_str[128];
	static struct cmd_ds_fwt_access fwt_access;
	char *ptr;

	ENTER();
	if (copy_from_user(in_str, wrq->u.data.pointer, sizeof(in_str)))
		return -EFAULT;

	if ((ptr = eth_str2addr(in_str, fwt_access.da)) == NULL) {
		lbs_pr_alert( "FWT_ADD: Invalid MAC address 1\n");
		return -EINVAL;
	}

	if ((ptr = eth_str2addr(ptr, fwt_access.ra)) == NULL) {
		lbs_pr_alert( "FWT_ADD: Invalid MAC address 2\n");
		return -EINVAL;
	}

	if ((ptr = next_param(ptr)))
		fwt_access.metric =
			cpu_to_le32(simple_strtoul(ptr, &ptr, 10));
	else
		fwt_access.metric = FWT_DEFAULT_METRIC;

	if ((ptr = next_param(ptr)))
		fwt_access.dir = (u8)simple_strtoul(ptr, &ptr, 10);
	else
		fwt_access.dir = FWT_DEFAULT_DIR;

	if ((ptr = next_param(ptr)))
		fwt_access.ssn =
			cpu_to_le32(simple_strtoul(ptr, &ptr, 10));
	else
		fwt_access.ssn = FWT_DEFAULT_SSN;

	if ((ptr = next_param(ptr)))
		fwt_access.dsn =
			cpu_to_le32(simple_strtoul(ptr, &ptr, 10));
	else
		fwt_access.dsn = FWT_DEFAULT_DSN;

	if ((ptr = next_param(ptr)))
		fwt_access.hopcount = simple_strtoul(ptr, &ptr, 10);
	else
		fwt_access.hopcount = FWT_DEFAULT_HOPCOUNT;

	if ((ptr = next_param(ptr)))
		fwt_access.ttl = simple_strtoul(ptr, &ptr, 10);
	else
		fwt_access.ttl = FWT_DEFAULT_TTL;

	if ((ptr = next_param(ptr)))
		fwt_access.expiration =
			cpu_to_le32(simple_strtoul(ptr, &ptr, 10));
	else
		fwt_access.expiration = FWT_DEFAULT_EXPIRATION;

	if ((ptr = next_param(ptr)))
		fwt_access.sleepmode = (u8)simple_strtoul(ptr, &ptr, 10);
	else
		fwt_access.sleepmode = FWT_DEFAULT_SLEEPMODE;

	if ((ptr = next_param(ptr)))
		fwt_access.snr =
			cpu_to_le32(simple_strtoul(ptr, &ptr, 10));
	else
		fwt_access.snr = FWT_DEFAULT_SNR;

#ifdef DEBUG
	{
		char ethaddr1_str[18], ethaddr2_str[18];
		eth_addr2str(fwt_access.da, ethaddr1_str);
		eth_addr2str(fwt_access.ra, ethaddr2_str);
		lbs_pr_debug(1, "FWT_ADD: adding (da:%s,%i,ra:%s)\n", ethaddr1_str,
		       fwt_access.dir, ethaddr2_str);
		lbs_pr_debug(1, "FWT_ADD: ssn:%u dsn:%u met:%u hop:%u ttl:%u exp:%u slp:%u snr:%u\n",
		       fwt_access.ssn, fwt_access.dsn, fwt_access.metric,
		       fwt_access.hopcount, fwt_access.ttl, fwt_access.expiration,
		       fwt_access.sleepmode, fwt_access.snr);
	}
#endif

	LEAVE();
	return (libertas_prepare_and_send_command(priv, cmd_fwt_access,
						  cmd_act_fwt_access_add,
						  cmd_option_waitforrsp, 0,
						  (void *)&fwt_access));
}

/**
 *  @brief          Delete an entry from the FWT table
 *  @param priv     A pointer to wlan_private structure
 *  @param req      A pointer to ifreq structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_fwt_del_ioctl(wlan_private * priv, struct ifreq *req)
{
	struct iwreq *wrq = (struct iwreq *)req;
	char in_str[64];
	static struct cmd_ds_fwt_access fwt_access;
	char *ptr;

	ENTER();
	if (copy_from_user(in_str, wrq->u.data.pointer, sizeof(in_str)))
		return -EFAULT;

	if ((ptr = eth_str2addr(in_str, fwt_access.da)) == NULL) {
		lbs_pr_alert( "FWT_DEL: Invalid MAC address 1\n");
		return -EINVAL;
	}

	if ((ptr = eth_str2addr(ptr, fwt_access.ra)) == NULL) {
		lbs_pr_alert( "FWT_DEL: Invalid MAC address 2\n");
		return -EINVAL;
	}

	if ((ptr = next_param(ptr)))
		fwt_access.dir = (u8)simple_strtoul(ptr, &ptr, 10);
	else
		fwt_access.dir = FWT_DEFAULT_DIR;

#ifdef DEBUG
	{
		char ethaddr1_str[18], ethaddr2_str[18];
		lbs_pr_debug(1, "FWT_DEL: line is %s\n", in_str);
		eth_addr2str(fwt_access.da, ethaddr1_str);
		eth_addr2str(fwt_access.ra, ethaddr2_str);
		lbs_pr_debug(1, "FWT_DEL: removing (da:%s,ra:%s,dir:%d)\n", ethaddr1_str,
		       ethaddr2_str, fwt_access.dir);
	}
#endif

	LEAVE();
	return (libertas_prepare_and_send_command(priv,
						  cmd_fwt_access,
						  cmd_act_fwt_access_del,
						  cmd_option_waitforrsp, 0,
						  (void *)&fwt_access));
}


/**
 *  @brief             Print route parameters
 *  @param fwt_access  struct cmd_ds_fwt_access with route info
 *  @param buf         destination buffer for route info
 */
static void print_route(struct cmd_ds_fwt_access fwt_access, char *buf)
{
	buf += sprintf(buf, " ");
	buf += eth_addr2str(fwt_access.da, buf);
	buf += sprintf(buf, " ");
	buf += eth_addr2str(fwt_access.ra, buf);
	buf += sprintf(buf, " %u", le32_to_cpu(fwt_access.metric));
	buf += sprintf(buf, " %u", fwt_access.dir);
	buf += sprintf(buf, " %u", le32_to_cpu(fwt_access.ssn));
	buf += sprintf(buf, " %u", le32_to_cpu(fwt_access.dsn));
	buf += sprintf(buf, " %u", fwt_access.hopcount);
	buf += sprintf(buf, " %u", fwt_access.ttl);
	buf += sprintf(buf, " %u", le32_to_cpu(fwt_access.expiration));
	buf += sprintf(buf, " %u", fwt_access.sleepmode);
	buf += sprintf(buf, " %u", le32_to_cpu(fwt_access.snr));
}

/**
 *  @brief          Lookup an entry in the FWT table
 *  @param priv     A pointer to wlan_private structure
 *  @param req      A pointer to ifreq structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_fwt_lookup_ioctl(wlan_private * priv, struct ifreq *req)
{
	struct iwreq *wrq = (struct iwreq *)req;
	char in_str[64];
	char *ptr;
	static struct cmd_ds_fwt_access fwt_access;
	static char out_str[128];
	int ret;

	ENTER();
	if (copy_from_user(in_str, wrq->u.data.pointer, sizeof(in_str)))
		return -EFAULT;

	if ((ptr = eth_str2addr(in_str, fwt_access.da)) == NULL) {
		lbs_pr_alert( "FWT_LOOKUP: Invalid MAC address\n");
		return -EINVAL;
	}

#ifdef DEBUG
	{
		char ethaddr1_str[18];
		lbs_pr_debug(1, "FWT_LOOKUP: line is %s\n", in_str);
		eth_addr2str(fwt_access.da, ethaddr1_str);
		lbs_pr_debug(1, "FWT_LOOKUP: looking for (da:%s)\n", ethaddr1_str);
	}
#endif

	ret = libertas_prepare_and_send_command(priv,
						cmd_fwt_access,
						cmd_act_fwt_access_lookup,
						cmd_option_waitforrsp, 0,
						(void *)&fwt_access);

	if (ret == 0)
		print_route(fwt_access, out_str);
	else
		sprintf(out_str, "(null)");

	wrq->u.data.length = strlen(out_str);
	if (copy_to_user(wrq->u.data.pointer, (char *)out_str,
			 wrq->u.data.length)) {
		lbs_pr_debug(1, "FWT_LOOKUP: Copy to user failed!\n");
		return -EFAULT;
	}

	LEAVE();
	return 0;
}

/**
 *  @brief          Reset all entries from the FWT table
 *  @param priv     A pointer to wlan_private structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_fwt_reset_ioctl(wlan_private * priv)
{
	lbs_pr_debug(1, "FWT: resetting\n");

	return (libertas_prepare_and_send_command(priv,
				      cmd_fwt_access,
				      cmd_act_fwt_access_reset,
				      cmd_option_waitforrsp, 0, NULL));
}

/**
 *  @brief          List an entry from the FWT table
 *  @param priv     A pointer to wlan_private structure
 *  @param req      A pointer to ifreq structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_fwt_list_ioctl(wlan_private * priv, struct ifreq *req)
{
	struct iwreq *wrq = (struct iwreq *)req;
	char in_str[8];
	static struct cmd_ds_fwt_access fwt_access;
	char *ptr = in_str;
	static char out_str[128];
	char *pbuf = out_str;
	int ret;

	ENTER();
	if (copy_from_user(in_str, wrq->u.data.pointer, sizeof(in_str)))
		return -EFAULT;

	fwt_access.id = cpu_to_le32(simple_strtoul(ptr, &ptr, 10));

#ifdef DEBUG
	{
		lbs_pr_debug(1, "FWT_LIST: line is %s\n", in_str);
		lbs_pr_debug(1, "FWT_LIST: listing id:%i\n", le32_to_cpu(fwt_access.id));
	}
#endif

	ret = libertas_prepare_and_send_command(priv, cmd_fwt_access,
				    cmd_act_fwt_access_list,
				    cmd_option_waitforrsp, 0, (void *)&fwt_access);

	if (ret == 0)
		print_route(fwt_access, pbuf);
	else
		pbuf += sprintf(pbuf, " (null)");

	wrq->u.data.length = strlen(out_str);
	if (copy_to_user(wrq->u.data.pointer, (char *)out_str,
			 wrq->u.data.length)) {
		lbs_pr_debug(1, "FWT_LIST: Copy to user failed!\n");
		return -EFAULT;
	}

	LEAVE();
	return 0;
}

/**
 *  @brief          List an entry from the FRT table
 *  @param priv     A pointer to wlan_private structure
 *  @param req      A pointer to ifreq structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_fwt_list_route_ioctl(wlan_private * priv, struct ifreq *req)
{
	struct iwreq *wrq = (struct iwreq *)req;
	char in_str[64];
	static struct cmd_ds_fwt_access fwt_access;
	char *ptr = in_str;
	static char out_str[128];
	char *pbuf = out_str;
	int ret;

	ENTER();
	if (copy_from_user(in_str, wrq->u.data.pointer, sizeof(in_str)))
		return -EFAULT;

	fwt_access.id = cpu_to_le32(simple_strtoul(ptr, &ptr, 10));

#ifdef DEBUG
	{
		lbs_pr_debug(1, "FWT_LIST_ROUTE: line is %s\n", in_str);
		lbs_pr_debug(1, "FWT_LIST_ROUTE: listing id:%i\n", le32_to_cpu(fwt_access.id));
	}
#endif

	ret = libertas_prepare_and_send_command(priv, cmd_fwt_access,
				    cmd_act_fwt_access_list_route,
				    cmd_option_waitforrsp, 0, (void *)&fwt_access);

	if (ret == 0) {
		pbuf += sprintf(pbuf, " ");
		pbuf += eth_addr2str(fwt_access.da, pbuf);
		pbuf += sprintf(pbuf, " %u", le32_to_cpu(fwt_access.metric));
		pbuf += sprintf(pbuf, " %u", fwt_access.dir);
		/* note that the firmware returns the nid in the id field */
		pbuf += sprintf(pbuf, " %u", le32_to_cpu(fwt_access.id));
		pbuf += sprintf(pbuf, " %u", le32_to_cpu(fwt_access.ssn));
		pbuf += sprintf(pbuf, " %u", le32_to_cpu(fwt_access.dsn));
		pbuf += sprintf(pbuf, "  hop %u", fwt_access.hopcount);
		pbuf += sprintf(pbuf, "  ttl %u", fwt_access.ttl);
		pbuf += sprintf(pbuf, " %u", le32_to_cpu(fwt_access.expiration));
	} else
		pbuf += sprintf(pbuf, " (null)");

	wrq->u.data.length = strlen(out_str);
	if (copy_to_user(wrq->u.data.pointer, (char *)out_str,
			 wrq->u.data.length)) {
		lbs_pr_debug(1, "FWT_LIST_ROUTE: Copy to user failed!\n");
		return -EFAULT;
	}

	LEAVE();
	return 0;
}

/**
 *  @brief          List an entry from the FNT table
 *  @param priv     A pointer to wlan_private structure
 *  @param req      A pointer to ifreq structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_fwt_list_neighbor_ioctl(wlan_private * priv, struct ifreq *req)
{
	struct iwreq *wrq = (struct iwreq *)req;
	char in_str[8];
	static struct cmd_ds_fwt_access fwt_access;
	char *ptr = in_str;
	static char out_str[128];
	char *pbuf = out_str;
	int ret;

	ENTER();
	if (copy_from_user(in_str, wrq->u.data.pointer, sizeof(in_str)))
		return -EFAULT;

	memset(&fwt_access, 0, sizeof(fwt_access));
	fwt_access.id = cpu_to_le32(simple_strtoul(ptr, &ptr, 10));

#ifdef DEBUG
	{
		lbs_pr_debug(1, "FWT_LIST_NEIGHBOR: line is %s\n", in_str);
		lbs_pr_debug(1, "FWT_LIST_NEIGHBOR: listing id:%i\n", le32_to_cpu(fwt_access.id));
	}
#endif

	ret = libertas_prepare_and_send_command(priv, cmd_fwt_access,
				    cmd_act_fwt_access_list_neighbor,
				    cmd_option_waitforrsp, 0,
				    (void *)&fwt_access);

	if (ret == 0) {
		pbuf += sprintf(pbuf, " ra ");
		pbuf += eth_addr2str(fwt_access.ra, pbuf);
		pbuf += sprintf(pbuf, "  slp %u", fwt_access.sleepmode);
		pbuf += sprintf(pbuf, "  snr %u", le32_to_cpu(fwt_access.snr));
		pbuf += sprintf(pbuf, "  ref %u", le32_to_cpu(fwt_access.references));
	} else
		pbuf += sprintf(pbuf, " (null)");

	wrq->u.data.length = strlen(out_str);
	if (copy_to_user(wrq->u.data.pointer, (char *)out_str,
			 wrq->u.data.length)) {
		lbs_pr_debug(1, "FWT_LIST_NEIGHBOR: Copy to user failed!\n");
		return -EFAULT;
	}

	LEAVE();
	return 0;
}

/**
 *  @brief          Cleans up the route (FRT) and neighbor (FNT) tables
 *                  (Garbage Collection)
 *  @param priv     A pointer to wlan_private structure
 *  @param req      A pointer to ifreq structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_fwt_cleanup_ioctl(wlan_private * priv, struct ifreq *req)
{
	struct iwreq *wrq = (struct iwreq *)req;
	static struct cmd_ds_fwt_access fwt_access;
	int ret;

	ENTER();

	lbs_pr_debug(1, "FWT: cleaning up\n");

	memset(&fwt_access, 0, sizeof(fwt_access));

	ret = libertas_prepare_and_send_command(priv, cmd_fwt_access,
				    cmd_act_fwt_access_cleanup,
				    cmd_option_waitforrsp, 0,
				    (void *)&fwt_access);

	if (ret == 0)
		wrq->u.param.value = le32_to_cpu(fwt_access.references);
	else
		return -EFAULT;

	LEAVE();
	return 0;
}

/**
 *  @brief          Gets firmware internal time (debug purposes)
 *  @param priv     A pointer to wlan_private structure
 *  @param req      A pointer to ifreq structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_fwt_time_ioctl(wlan_private * priv, struct ifreq *req)
{
	struct iwreq *wrq = (struct iwreq *)req;
	static struct cmd_ds_fwt_access fwt_access;
	int ret;

	ENTER();

	lbs_pr_debug(1, "FWT: getting time\n");

	memset(&fwt_access, 0, sizeof(fwt_access));

	ret = libertas_prepare_and_send_command(priv, cmd_fwt_access,
				    cmd_act_fwt_access_time,
				    cmd_option_waitforrsp, 0,
				    (void *)&fwt_access);

	if (ret == 0)
		wrq->u.param.value = le32_to_cpu(fwt_access.references);
	else
		return -EFAULT;

	LEAVE();
	return 0;
}

/**
 *  @brief          Gets mesh ttl from firmware
 *  @param priv     A pointer to wlan_private structure
 *  @param req      A pointer to ifreq structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_mesh_get_ttl_ioctl(wlan_private * priv, struct ifreq *req)
{
	struct iwreq *wrq = (struct iwreq *)req;
	struct cmd_ds_mesh_access mesh_access;
	int ret;

	ENTER();

	memset(&mesh_access, 0, sizeof(mesh_access));

	ret = libertas_prepare_and_send_command(priv, cmd_mesh_access,
				    cmd_act_mesh_get_ttl,
				    cmd_option_waitforrsp, 0,
				    (void *)&mesh_access);

	if (ret == 0)
		wrq->u.param.value = le32_to_cpu(mesh_access.data[0]);
	else
		return -EFAULT;

	LEAVE();
	return 0;
}

/**
 *  @brief          Gets mesh ttl from firmware
 *  @param priv     A pointer to wlan_private structure
 *  @param ttl      New ttl value
 *  @return         0 --success, otherwise fail
 */
static int wlan_mesh_set_ttl_ioctl(wlan_private * priv, int ttl)
{
	struct cmd_ds_mesh_access mesh_access;
	int ret;

	ENTER();

	if( (ttl > 0xff) || (ttl < 0) )
		return -EINVAL;

	memset(&mesh_access, 0, sizeof(mesh_access));
	mesh_access.data[0] = ttl;

	ret = libertas_prepare_and_send_command(priv, cmd_mesh_access,
						cmd_act_mesh_set_ttl,
						cmd_option_waitforrsp, 0,
						(void *)&mesh_access);

	if (ret != 0)
		ret = -EFAULT;

	LEAVE();
	return ret;
}

/**
 *  @brief ioctl function - entry point
 *
 *  @param dev		A pointer to net_device structure
 *  @param req	   	A pointer to ifreq structure
 *  @param cmd 		command
 *  @return 	   	0--success, otherwise fail
 */
int libertas_do_ioctl(struct net_device *dev, struct ifreq *req, int cmd)
{
	int subcmd = 0;
	int idata = 0;
	int *pdata;
	int ret = 0;
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;
	struct iwreq *wrq = (struct iwreq *)req;

	ENTER();

	lbs_pr_debug(1, "libertas_do_ioctl: ioctl cmd = 0x%x\n", cmd);
	switch (cmd) {
	case WLAN_SETNONE_GETNONE:	/* set WPA mode on/off ioctl #20 */
		switch (wrq->u.data.flags) {
		case WLAN_SUBCMD_BT_RESET:	/* bt_reset */
			wlan_bt_reset_ioctl(priv);
			break;
		case WLAN_SUBCMD_FWT_RESET:	/* fwt_reset */
			wlan_fwt_reset_ioctl(priv);
			break;
		}		/* End of switch */
		break;

	case WLAN_SETONEINT_GETNONE:
		/* The first 4 bytes of req->ifr_data is sub-ioctl number
		 * after 4 bytes sits the payload.
		 */
		subcmd = wrq->u.data.flags;
		if (!subcmd)
			subcmd = (int)wrq->u.param.value;

		switch (subcmd) {
		case WLANSETREGION:
			idata = SUBCMD_DATA(wrq);
			ret = wlan_set_region(priv, (u16) idata);
			break;
		case WLAN_SUBCMD_MESH_SET_TTL:
			idata = SUBCMD_DATA(wrq);
			ret = wlan_mesh_set_ttl_ioctl(priv, idata);
			break;

		default:
			ret = -EOPNOTSUPP;
			break;
		}

		break;

	case WLAN_SET128CHAR_GET128CHAR:
		switch ((int)wrq->u.data.flags) {
		case WLAN_SUBCMD_BT_ADD:
			ret = wlan_bt_add_ioctl(priv, req);
			break;
		case WLAN_SUBCMD_BT_DEL:
			ret = wlan_bt_del_ioctl(priv, req);
			break;
		case WLAN_SUBCMD_BT_LIST:
			ret = wlan_bt_list_ioctl(priv, req);
			break;
		case WLAN_SUBCMD_FWT_ADD:
			ret = wlan_fwt_add_ioctl(priv, req);
			break;
		case WLAN_SUBCMD_FWT_DEL:
			ret = wlan_fwt_del_ioctl(priv, req);
			break;
		case WLAN_SUBCMD_FWT_LOOKUP:
			ret = wlan_fwt_lookup_ioctl(priv, req);
			break;
		case WLAN_SUBCMD_FWT_LIST_NEIGHBOR:
			ret = wlan_fwt_list_neighbor_ioctl(priv, req);
			break;
		case WLAN_SUBCMD_FWT_LIST:
			ret = wlan_fwt_list_ioctl(priv, req);
			break;
		case WLAN_SUBCMD_FWT_LIST_ROUTE:
			ret = wlan_fwt_list_route_ioctl(priv, req);
			break;
		}
		break;

	case WLAN_SETNONE_GETONEINT:
		switch (wrq->u.param.value) {
		case WLANGETREGION:
			pdata = (int *)wrq->u.name;
			*pdata = (int)adapter->regioncode;
			break;
		case WLAN_SUBCMD_FWT_CLEANUP:	/* fwt_cleanup */
			ret = wlan_fwt_cleanup_ioctl(priv, req);
			break;

		case WLAN_SUBCMD_FWT_TIME:	/* fwt_time */
			ret = wlan_fwt_time_ioctl(priv, req);
			break;

		case WLAN_SUBCMD_MESH_GET_TTL:
			ret = wlan_mesh_get_ttl_ioctl(priv, req);
			break;

		default:
			ret = -EOPNOTSUPP;

		}

		break;

	case WLAN_SET_GET_SIXTEEN_INT:
		switch ((int)wrq->u.data.flags) {
		case WLAN_LED_GPIO_CTRL:
			{
				int i;
				int data[16];

				struct cmd_ds_802_11_led_ctrl ctrl;
				struct mrvlietypes_ledgpio *gpio =
				    (struct mrvlietypes_ledgpio *) ctrl.data;

				memset(&ctrl, 0, sizeof(ctrl));
				if (wrq->u.data.length > MAX_LEDS * 2)
					return -ENOTSUPP;
				if ((wrq->u.data.length % 2) != 0)
					return -ENOTSUPP;
				if (wrq->u.data.length == 0) {
					ctrl.action =
					    cpu_to_le16
					    (cmd_act_get);
				} else {
					if (copy_from_user
					    (data, wrq->u.data.pointer,
					     sizeof(int) *
					     wrq->u.data.length)) {
						lbs_pr_debug(1,
						       "Copy from user failed\n");
						return -EFAULT;
					}

					ctrl.action =
					    cpu_to_le16
					    (cmd_act_set);
					ctrl.numled = cpu_to_le16(0);
					gpio->header.type =
					    cpu_to_le16(TLV_TYPE_LED_GPIO);
					gpio->header.len = wrq->u.data.length;
					for (i = 0; i < wrq->u.data.length;
					     i += 2) {
						gpio->ledpin[i / 2].led =
						    data[i];
						gpio->ledpin[i / 2].pin =
						    data[i + 1];
					}
				}
				ret =
				    libertas_prepare_and_send_command(priv,
							  cmd_802_11_led_gpio_ctrl,
							  0,
							  cmd_option_waitforrsp,
							  0, (void *)&ctrl);
				for (i = 0; i < gpio->header.len; i += 2) {
					data[i] = gpio->ledpin[i / 2].led;
					data[i + 1] = gpio->ledpin[i / 2].pin;
				}
				if (copy_to_user(wrq->u.data.pointer, data,
						 sizeof(int) *
						 gpio->header.len)) {
					lbs_pr_debug(1, "Copy to user failed\n");
					return -EFAULT;
				}

				wrq->u.data.length = gpio->header.len;
			}
			break;
		}
		break;

	default:
		ret = -EINVAL;
		break;
	}
	LEAVE();
	return ret;
}



/*
 * iwmc3200top - Intel Wireless MultiCom 3200 Top Driver
 * drivers/misc/iwmc3200top/log.c
 *
 * Copyright (C) 2009 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * Author Name: Maxim Grabarnik <maxim.grabarnink@intel.com>
 *  -
 *
 */

#include <linux/kernel.h>
#include <linux/mmc/sdio_func.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include "fw-msg.h"
#include "iwmc3200top.h"
#include "log.h"

/* Maximal hexadecimal string size of the FW memdump message */
#define LOG_MSG_SIZE_MAX		12400

/* iwmct_logdefs is a global used by log macros */
u8 iwmct_logdefs[LOG_SRC_MAX];
static u8 iwmct_fw_logdefs[FW_LOG_SRC_MAX];


static int _log_set_log_filter(u8 *logdefs, int size, u8 src, u8 logmask)
{
	int i;

	if (src < size)
		logdefs[src] = logmask;
	else if (src == LOG_SRC_ALL)
		for (i = 0; i < size; i++)
			logdefs[i] = logmask;
	else
		return -1;

	return 0;
}


int iwmct_log_set_filter(u8 src, u8 logmask)
{
	return _log_set_log_filter(iwmct_logdefs, LOG_SRC_MAX, src, logmask);
}


int iwmct_log_set_fw_filter(u8 src, u8 logmask)
{
	return _log_set_log_filter(iwmct_fw_logdefs,
				   FW_LOG_SRC_MAX, src, logmask);
}


static int log_msg_format_hex(char *str, int slen, u8 *ibuf,
			      int ilen, char *pref)
{
	int pos = 0;
	int i;
	int len;

	for (pos = 0, i = 0; pos < slen - 2 && pref[i] != '\0'; i++, pos++)
		str[pos] = pref[i];

	for (i = 0; pos < slen - 2 && i < ilen; pos += len, i++)
		len = snprintf(&str[pos], slen - pos - 1, " %2.2X", ibuf[i]);

	if (i < ilen)
		return -1;

	return 0;
}

/*	NOTE: This function is not thread safe.
	Currently it's called only from sdio rx worker - no race there
*/
void iwmct_log_top_message(struct iwmct_priv *priv, u8 *buf, int len)
{
	struct top_msg *msg;
	static char logbuf[LOG_MSG_SIZE_MAX];

	msg = (struct top_msg *)buf;

	if (len < sizeof(msg->hdr) + sizeof(msg->u.log.log_hdr)) {
		LOG_ERROR(priv, FW_MSG, "Log message from TOP "
			  "is too short %d (expected %zd)\n",
			  len, sizeof(msg->hdr) + sizeof(msg->u.log.log_hdr));
		return;
	}

	if (!(iwmct_fw_logdefs[msg->u.log.log_hdr.logsource] &
		BIT(msg->u.log.log_hdr.severity)) ||
	    !(iwmct_logdefs[LOG_SRC_FW_MSG] & BIT(msg->u.log.log_hdr.severity)))
		return;

	switch (msg->hdr.category) {
	case COMM_CATEGORY_TESTABILITY:
		if (!(iwmct_logdefs[LOG_SRC_TST] &
		      BIT(msg->u.log.log_hdr.severity)))
			return;
		if (log_msg_format_hex(logbuf, LOG_MSG_SIZE_MAX, buf,
				       le16_to_cpu(msg->hdr.length) +
				       sizeof(msg->hdr), "<TST>"))
			LOG_WARNING(priv, TST,
				  "TOP TST message is too long, truncating...");
		LOG_WARNING(priv, TST, "%s\n", logbuf);
		break;
	case COMM_CATEGORY_DEBUG:
		if (msg->hdr.opcode == OP_DBG_ZSTR_MSG)
			LOG_INFO(priv, FW_MSG, "%s %s", "<DBG>",
				       ((u8 *)msg) + sizeof(msg->hdr)
					+ sizeof(msg->u.log.log_hdr));
		else {
			if (log_msg_format_hex(logbuf, LOG_MSG_SIZE_MAX, buf,
					le16_to_cpu(msg->hdr.length)
						+ sizeof(msg->hdr),
					"<DBG>"))
				LOG_WARNING(priv, FW_MSG,
					"TOP DBG message is too long,"
					"truncating...");
			LOG_WARNING(priv, FW_MSG, "%s\n", logbuf);
		}
		break;
	default:
		break;
	}
}

static int _log_get_filter_str(u8 *logdefs, int logdefsz, char *buf, int size)
{
	int i, pos, len;
	for (i = 0, pos = 0; (pos < size-1) && (i < logdefsz); i++) {
		len = snprintf(&buf[pos], size - pos - 1, "0x%02X%02X,",
				i, logdefs[i]);
		pos += len;
	}
	buf[pos-1] = '\n';
	buf[pos] = '\0';

	if (i < logdefsz)
		return -1;
	return 0;
}

int log_get_filter_str(char *buf, int size)
{
	return _log_get_filter_str(iwmct_logdefs, LOG_SRC_MAX, buf, size);
}

int log_get_fw_filter_str(char *buf, int size)
{
	return _log_get_filter_str(iwmct_fw_logdefs, FW_LOG_SRC_MAX, buf, size);
}

#define HEXADECIMAL_RADIX	16
#define LOG_SRC_FORMAT		7 /* log level is in format of "0xXXXX," */

ssize_t show_iwmct_log_level(struct device *d,
				struct device_attribute *attr, char *buf)
{
	struct iwmct_priv *priv = dev_get_drvdata(d);
	char *str_buf;
	int buf_size;
	ssize_t ret;

	buf_size = (LOG_SRC_FORMAT * LOG_SRC_MAX) + 1;
	str_buf = kzalloc(buf_size, GFP_KERNEL);
	if (!str_buf) {
		LOG_ERROR(priv, DEBUGFS,
			"failed to allocate %d bytes\n", buf_size);
		ret = -ENOMEM;
		goto exit;
	}

	if (log_get_filter_str(str_buf, buf_size) < 0) {
		ret = -EINVAL;
		goto exit;
	}

	ret = sprintf(buf, "%s", str_buf);

exit:
	kfree(str_buf);
	return ret;
}

ssize_t store_iwmct_log_level(struct device *d,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct iwmct_priv *priv = dev_get_drvdata(d);
	char *token, *str_buf = NULL;
	long val;
	ssize_t ret = count;
	u8 src, mask;

	if (!count)
		goto exit;

	str_buf = kzalloc(count, GFP_KERNEL);
	if (!str_buf) {
		LOG_ERROR(priv, DEBUGFS,
			"failed to allocate %zd bytes\n", count);
		ret = -ENOMEM;
		goto exit;
	}

	memcpy(str_buf, buf, count);

	while ((token = strsep(&str_buf, ",")) != NULL) {
		while (isspace(*token))
			++token;
		if (strict_strtol(token, HEXADECIMAL_RADIX, &val)) {
			LOG_ERROR(priv, DEBUGFS,
				  "failed to convert string to long %s\n",
				  token);
			ret = -EINVAL;
			goto exit;
		}

		mask  = val & 0xFF;
		src = (val & 0XFF00) >> 8;
		iwmct_log_set_filter(src, mask);
	}

exit:
	kfree(str_buf);
	return ret;
}

ssize_t show_iwmct_log_level_fw(struct device *d,
			struct device_attribute *attr, char *buf)
{
	struct iwmct_priv *priv = dev_get_drvdata(d);
	char *str_buf;
	int buf_size;
	ssize_t ret;

	buf_size = (LOG_SRC_FORMAT * FW_LOG_SRC_MAX) + 2;

	str_buf = kzalloc(buf_size, GFP_KERNEL);
	if (!str_buf) {
		LOG_ERROR(priv, DEBUGFS,
			"failed to allocate %d bytes\n", buf_size);
		ret = -ENOMEM;
		goto exit;
	}

	if (log_get_fw_filter_str(str_buf, buf_size) < 0) {
		ret = -EINVAL;
		goto exit;
	}

	ret = sprintf(buf, "%s", str_buf);

exit:
	kfree(str_buf);
	return ret;
}

ssize_t store_iwmct_log_level_fw(struct device *d,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct iwmct_priv *priv = dev_get_drvdata(d);
	struct top_msg cmd;
	char *token, *str_buf = NULL;
	ssize_t ret = count;
	u16 cmdlen = 0;
	int i;
	long val;
	u8 src, mask;

	if (!count)
		goto exit;

	str_buf = kzalloc(count, GFP_KERNEL);
	if (!str_buf) {
		LOG_ERROR(priv, DEBUGFS,
			"failed to allocate %zd bytes\n", count);
		ret = -ENOMEM;
		goto exit;
	}

	memcpy(str_buf, buf, count);

	cmd.hdr.type = COMM_TYPE_H2D;
	cmd.hdr.category = COMM_CATEGORY_DEBUG;
	cmd.hdr.opcode = CMD_DBG_LOG_LEVEL;

	for (i = 0; ((token = strsep(&str_buf, ",")) != NULL) &&
		     (i < FW_LOG_SRC_MAX); i++) {

		while (isspace(*token))
			++token;

		if (strict_strtol(token, HEXADECIMAL_RADIX, &val)) {
			LOG_ERROR(priv, DEBUGFS,
				  "failed to convert string to long %s\n",
				  token);
			ret = -EINVAL;
			goto exit;
		}

		mask  = val & 0xFF; /* LSB */
		src = (val & 0XFF00) >> 8; /* 2nd least significant byte. */
		iwmct_log_set_fw_filter(src, mask);

		cmd.u.logdefs[i].logsource = src;
		cmd.u.logdefs[i].sevmask = mask;
	}

	cmd.hdr.length = cpu_to_le16(i * sizeof(cmd.u.logdefs[0]));
	cmdlen = (i * sizeof(cmd.u.logdefs[0]) + sizeof(cmd.hdr));

	ret = iwmct_send_hcmd(priv, (u8 *)&cmd, cmdlen);
	if (ret) {
		LOG_ERROR(priv, DEBUGFS,
			  "Failed to send %d bytes of fwcmd, ret=%zd\n",
			  cmdlen, ret);
		goto exit;
	} else
		LOG_INFO(priv, DEBUGFS, "fwcmd sent (%d bytes)\n", cmdlen);

	ret = count;

exit:
	kfree(str_buf);
	return ret;
}


// SPDX-License-Identifier: GPL-2.0-or-later
/****************************************************************

 Siano Mobile Silicon, Inc.
 MDTV receiver kernel modules.
 Copyright (C) 2006-2009, Uri Shkolnik


 ****************************************************************/

#include <linux/export.h>
#include <asm/byteorder.h>

#include "smsendian.h"
#include "smscoreapi.h"

void smsendian_handle_tx_message(void *buffer)
{
#ifdef __BIG_ENDIAN
	struct sms_msg_data *msg = buffer;
	int i;
	int msg_words;
	u32 *msg_data = &msg->msg_data;

	switch (msg->x_msg_header.msg_type) {
	case MSG_SMS_DATA_DOWNLOAD_REQ:
	{
		msg->msg_data = le32_to_cpu((__force __le32)(msg->msg_data));
		break;
	}

	default:
		msg_words = (msg->x_msg_header.msg_length -
				sizeof(struct sms_msg_hdr))/4;

		for (i = 0; i < msg_words; i++)
			msg_data[i] = le32_to_cpu((__force __le32)msg_data[i]);

		break;
	}
#endif /* __BIG_ENDIAN */
}
EXPORT_SYMBOL_GPL(smsendian_handle_tx_message);

void smsendian_handle_rx_message(void *buffer)
{
#ifdef __BIG_ENDIAN
	struct sms_msg_data *msg = (struct sms_msg_data *)buffer;
	int i;
	int msg_words;

	switch (msg->x_msg_header.msg_type) {
	case MSG_SMS_GET_VERSION_EX_RES:
	{
		struct sms_version_res *ver =
			(struct sms_version_res *) msg;
		ver->chip_model = le16_to_cpu((__force __le16)ver->chip_model);
		break;
	}

	case MSG_SMS_DVBT_BDA_DATA:
	case MSG_SMS_DAB_CHANNEL:
	case MSG_SMS_DATA_MSG:
	{
		break;
	}

	default:
	{
		u32 *msg_data = &msg->msg_data;
		msg_words = (msg->x_msg_header.msg_length -
				sizeof(struct sms_msg_hdr))/4;

		for (i = 0; i < msg_words; i++)
			msg_data[i] = le32_to_cpu((__force __le32)msg_data[i]);

		break;
	}
	}
#endif /* __BIG_ENDIAN */
}
EXPORT_SYMBOL_GPL(smsendian_handle_rx_message);

void smsendian_handle_message_header(void *msg)
{
#ifdef __BIG_ENDIAN
	struct sms_msg_hdr *phdr = (struct sms_msg_hdr *)msg;

	phdr->msg_type = le16_to_cpu((__force __le16)phdr->msg_type);
	phdr->msg_length = le16_to_cpu((__force __le16)phdr->msg_length);
	phdr->msg_flags = le16_to_cpu((__force __le16)phdr->msg_flags);
#endif /* __BIG_ENDIAN */
}
EXPORT_SYMBOL_GPL(smsendian_handle_message_header);

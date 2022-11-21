// SPDX-License-Identifier: GPL-2.0-or-later
/* -------------------------------------------------------------------------
 * Copyright (C) 2014-2016, Intel Corporation
 *
 * -------------------------------------------------------------------------
 */

#include <linux/module.h>
#include <linux/nfc.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <net/nfc/nci_core.h>

#include "fdp.h"

#define FDP_OTP_PATCH_NAME			"otp.bin"
#define FDP_RAM_PATCH_NAME			"ram.bin"
#define FDP_FW_HEADER_SIZE			576
#define FDP_FW_UPDATE_SLEEP			1000

#define NCI_GET_VERSION_TIMEOUT			8000
#define NCI_PATCH_REQUEST_TIMEOUT		8000
#define FDP_PATCH_CONN_DEST			0xC2
#define FDP_PATCH_CONN_PARAM_TYPE		0xA0

#define NCI_PATCH_TYPE_RAM			0x00
#define NCI_PATCH_TYPE_OTP			0x01
#define NCI_PATCH_TYPE_EOT			0xFF

#define NCI_PARAM_ID_FW_RAM_VERSION		0xA0
#define NCI_PARAM_ID_FW_OTP_VERSION		0xA1
#define NCI_PARAM_ID_OTP_LIMITED_VERSION	0xC5
#define NCI_PARAM_ID_KEY_INDEX_ID		0xC6

#define NCI_GID_PROP				0x0F
#define NCI_OP_PROP_PATCH_OID			0x08
#define NCI_OP_PROP_SET_PDATA_OID		0x23

struct fdp_nci_info {
	const struct nfc_phy_ops *phy_ops;
	struct fdp_i2c_phy *phy;
	struct nci_dev *ndev;

	const struct firmware *otp_patch;
	const struct firmware *ram_patch;
	u32 otp_patch_version;
	u32 ram_patch_version;

	u32 otp_version;
	u32 ram_version;
	u32 limited_otp_version;
	u8 key_index;

	const u8 *fw_vsc_cfg;
	u8 clock_type;
	u32 clock_freq;

	atomic_t data_pkt_counter;
	void (*data_pkt_counter_cb)(struct nci_dev *ndev);
	u8 setup_patch_sent;
	u8 setup_patch_ntf;
	u8 setup_patch_status;
	u8 setup_reset_ntf;
	wait_queue_head_t setup_wq;
};

static const u8 nci_core_get_config_otp_ram_version[5] = {
	0x04,
	NCI_PARAM_ID_FW_RAM_VERSION,
	NCI_PARAM_ID_FW_OTP_VERSION,
	NCI_PARAM_ID_OTP_LIMITED_VERSION,
	NCI_PARAM_ID_KEY_INDEX_ID
};

struct nci_core_get_config_rsp {
	u8 status;
	u8 count;
	u8 data[];
};

static int fdp_nci_create_conn(struct nci_dev *ndev)
{
	struct fdp_nci_info *info = nci_get_drvdata(ndev);
	struct core_conn_create_dest_spec_params param;
	int r;

	/* proprietary destination specific paramerer without value */
	param.type = FDP_PATCH_CONN_PARAM_TYPE;
	param.length = 0x00;

	r = nci_core_conn_create(info->ndev, FDP_PATCH_CONN_DEST, 1,
				 sizeof(param), &param);
	if (r)
		return r;

	return nci_get_conn_info_by_dest_type_params(ndev,
						     FDP_PATCH_CONN_DEST, NULL);
}

static inline int fdp_nci_get_versions(struct nci_dev *ndev)
{
	return nci_core_cmd(ndev, NCI_OP_CORE_GET_CONFIG_CMD,
			    sizeof(nci_core_get_config_otp_ram_version),
			    (__u8 *) &nci_core_get_config_otp_ram_version);
}

static inline int fdp_nci_patch_cmd(struct nci_dev *ndev, u8 type)
{
	return nci_prop_cmd(ndev, NCI_OP_PROP_PATCH_OID, sizeof(type), &type);
}

static inline int fdp_nci_set_production_data(struct nci_dev *ndev, u8 len,
					      const char *data)
{
	return nci_prop_cmd(ndev, NCI_OP_PROP_SET_PDATA_OID, len, data);
}

static int fdp_nci_set_clock(struct nci_dev *ndev, u8 clock_type,
			     u32 clock_freq)
{
	u32 fc = 13560;
	u32 nd, num, delta;
	char data[9];

	nd = (24 * fc) / clock_freq;
	delta = 24 * fc - nd * clock_freq;
	num = (32768 * delta) / clock_freq;

	data[0] = 0x00;
	data[1] = 0x00;
	data[2] = 0x00;

	data[3] = 0x10;
	data[4] = 0x04;
	data[5] = num & 0xFF;
	data[6] = (num >> 8) & 0xff;
	data[7] = nd;
	data[8] = clock_type;

	return fdp_nci_set_production_data(ndev, 9, data);
}

static void fdp_nci_send_patch_cb(struct nci_dev *ndev)
{
	struct fdp_nci_info *info = nci_get_drvdata(ndev);

	info->setup_patch_sent = 1;
	wake_up(&info->setup_wq);
}

/*
 * Register a packet sent counter and a callback
 *
 * We have no other way of knowing when all firmware packets were sent out
 * on the i2c bus. We need to know that in order to close the connection and
 * send the patch end message.
 */
static void fdp_nci_set_data_pkt_counter(struct nci_dev *ndev,
				  void (*cb)(struct nci_dev *ndev), int count)
{
	struct fdp_nci_info *info = nci_get_drvdata(ndev);
	struct device *dev = &info->phy->i2c_dev->dev;

	dev_dbg(dev, "NCI data pkt counter %d\n", count);
	atomic_set(&info->data_pkt_counter, count);
	info->data_pkt_counter_cb = cb;
}

/*
 * The device is expecting a stream of packets. All packets need to
 * have the PBF flag set to 0x0 (last packet) even if the firmware
 * file is segmented and there are multiple packets. If we give the
 * whole firmware to nci_send_data it will segment it and it will set
 * the PBF flag to 0x01 so we need to do the segmentation here.
 *
 * The firmware will be analyzed and applied when we send NCI_OP_PROP_PATCH_CMD
 * command with NCI_PATCH_TYPE_EOT parameter. The device will send a
 * NFCC_PATCH_NTF packet and a NCI_OP_CORE_RESET_NTF packet.
 */
static int fdp_nci_send_patch(struct nci_dev *ndev, u8 conn_id, u8 type)
{
	struct fdp_nci_info *info = nci_get_drvdata(ndev);
	const struct firmware *fw;
	struct sk_buff *skb;
	unsigned long len;
	int max_size, payload_size;
	int rc = 0;

	if ((type == NCI_PATCH_TYPE_OTP && !info->otp_patch) ||
	    (type == NCI_PATCH_TYPE_RAM && !info->ram_patch))
		return -EINVAL;

	if (type == NCI_PATCH_TYPE_OTP)
		fw = info->otp_patch;
	else
		fw = info->ram_patch;

	max_size = nci_conn_max_data_pkt_payload_size(ndev, conn_id);
	if (max_size <= 0)
		return -EINVAL;

	len = fw->size;

	fdp_nci_set_data_pkt_counter(ndev, fdp_nci_send_patch_cb,
				     DIV_ROUND_UP(fw->size, max_size));

	while (len) {

		payload_size = min_t(unsigned long, max_size, len);

		skb = nci_skb_alloc(ndev, (NCI_CTRL_HDR_SIZE + payload_size),
				    GFP_KERNEL);
		if (!skb) {
			fdp_nci_set_data_pkt_counter(ndev, NULL, 0);
			return -ENOMEM;
		}


		skb_reserve(skb, NCI_CTRL_HDR_SIZE);

		skb_put_data(skb, fw->data + (fw->size - len), payload_size);

		rc = nci_send_data(ndev, conn_id, skb);

		if (rc) {
			fdp_nci_set_data_pkt_counter(ndev, NULL, 0);
			return rc;
		}

		len -= payload_size;
	}

	return rc;
}

static int fdp_nci_open(struct nci_dev *ndev)
{
	const struct fdp_nci_info *info = nci_get_drvdata(ndev);

	return info->phy_ops->enable(info->phy);
}

static int fdp_nci_close(struct nci_dev *ndev)
{
	return 0;
}

static int fdp_nci_send(struct nci_dev *ndev, struct sk_buff *skb)
{
	struct fdp_nci_info *info = nci_get_drvdata(ndev);

	if (atomic_dec_and_test(&info->data_pkt_counter))
		info->data_pkt_counter_cb(ndev);

	return info->phy_ops->write(info->phy, skb);
}

static int fdp_nci_request_firmware(struct nci_dev *ndev)
{
	struct fdp_nci_info *info = nci_get_drvdata(ndev);
	struct device *dev = &info->phy->i2c_dev->dev;
	const u8 *data;
	int r;

	r = request_firmware(&info->ram_patch, FDP_RAM_PATCH_NAME, dev);
	if (r < 0) {
		nfc_err(dev, "RAM patch request error\n");
		return r;
	}

	data = info->ram_patch->data;
	info->ram_patch_version =
		data[FDP_FW_HEADER_SIZE] |
		(data[FDP_FW_HEADER_SIZE + 1] << 8) |
		(data[FDP_FW_HEADER_SIZE + 2] << 16) |
		(data[FDP_FW_HEADER_SIZE + 3] << 24);

	dev_dbg(dev, "RAM patch version: %d, size: %zu\n",
		  info->ram_patch_version, info->ram_patch->size);


	r = request_firmware(&info->otp_patch, FDP_OTP_PATCH_NAME, dev);
	if (r < 0) {
		nfc_err(dev, "OTP patch request error\n");
		return 0;
	}

	data = (u8 *) info->otp_patch->data;
	info->otp_patch_version =
		data[FDP_FW_HEADER_SIZE] |
		(data[FDP_FW_HEADER_SIZE + 1] << 8) |
		(data[FDP_FW_HEADER_SIZE+2] << 16) |
		(data[FDP_FW_HEADER_SIZE+3] << 24);

	dev_dbg(dev, "OTP patch version: %d, size: %zu\n",
		 info->otp_patch_version, info->otp_patch->size);
	return 0;
}

static void fdp_nci_release_firmware(struct nci_dev *ndev)
{
	struct fdp_nci_info *info = nci_get_drvdata(ndev);

	if (info->otp_patch) {
		release_firmware(info->otp_patch);
		info->otp_patch = NULL;
	}

	if (info->ram_patch) {
		release_firmware(info->ram_patch);
		info->ram_patch = NULL;
	}
}

static int fdp_nci_patch_otp(struct nci_dev *ndev)
{
	struct fdp_nci_info *info = nci_get_drvdata(ndev);
	struct device *dev = &info->phy->i2c_dev->dev;
	int conn_id;
	int r = 0;

	if (info->otp_version >= info->otp_patch_version)
		return r;

	info->setup_patch_sent = 0;
	info->setup_reset_ntf = 0;
	info->setup_patch_ntf = 0;

	/* Patch init request */
	r = fdp_nci_patch_cmd(ndev, NCI_PATCH_TYPE_OTP);
	if (r)
		return r;

	/* Patch data connection creation */
	conn_id = fdp_nci_create_conn(ndev);
	if (conn_id < 0)
		return conn_id;

	/* Send the patch over the data connection */
	r = fdp_nci_send_patch(ndev, conn_id, NCI_PATCH_TYPE_OTP);
	if (r)
		return r;

	/* Wait for all the packets to be send over i2c */
	wait_event_interruptible(info->setup_wq,
				 info->setup_patch_sent == 1);

	/* make sure that the NFCC processed the last data packet */
	msleep(FDP_FW_UPDATE_SLEEP);

	/* Close the data connection */
	r = nci_core_conn_close(info->ndev, conn_id);
	if (r)
		return r;

	/* Patch finish message */
	if (fdp_nci_patch_cmd(ndev, NCI_PATCH_TYPE_EOT)) {
		nfc_err(dev, "OTP patch error 0x%x\n", r);
		return -EINVAL;
	}

	/* If the patch notification didn't arrive yet, wait for it */
	wait_event_interruptible(info->setup_wq, info->setup_patch_ntf);

	/* Check if the patching was successful */
	r = info->setup_patch_status;
	if (r) {
		nfc_err(dev, "OTP patch error 0x%x\n", r);
		return -EINVAL;
	}

	/*
	 * We need to wait for the reset notification before we
	 * can continue
	 */
	wait_event_interruptible(info->setup_wq, info->setup_reset_ntf);

	return r;
}

static int fdp_nci_patch_ram(struct nci_dev *ndev)
{
	struct fdp_nci_info *info = nci_get_drvdata(ndev);
	struct device *dev = &info->phy->i2c_dev->dev;
	int conn_id;
	int r = 0;

	if (info->ram_version >= info->ram_patch_version)
		return r;

	info->setup_patch_sent = 0;
	info->setup_reset_ntf = 0;
	info->setup_patch_ntf = 0;

	/* Patch init request */
	r = fdp_nci_patch_cmd(ndev, NCI_PATCH_TYPE_RAM);
	if (r)
		return r;

	/* Patch data connection creation */
	conn_id = fdp_nci_create_conn(ndev);
	if (conn_id < 0)
		return conn_id;

	/* Send the patch over the data connection */
	r = fdp_nci_send_patch(ndev, conn_id, NCI_PATCH_TYPE_RAM);
	if (r)
		return r;

	/* Wait for all the packets to be send over i2c */
	wait_event_interruptible(info->setup_wq,
				 info->setup_patch_sent == 1);

	/* make sure that the NFCC processed the last data packet */
	msleep(FDP_FW_UPDATE_SLEEP);

	/* Close the data connection */
	r = nci_core_conn_close(info->ndev, conn_id);
	if (r)
		return r;

	/* Patch finish message */
	if (fdp_nci_patch_cmd(ndev, NCI_PATCH_TYPE_EOT)) {
		nfc_err(dev, "RAM patch error 0x%x\n", r);
		return -EINVAL;
	}

	/* If the patch notification didn't arrive yet, wait for it */
	wait_event_interruptible(info->setup_wq, info->setup_patch_ntf);

	/* Check if the patching was successful */
	r = info->setup_patch_status;
	if (r) {
		nfc_err(dev, "RAM patch error 0x%x\n", r);
		return -EINVAL;
	}

	/*
	 * We need to wait for the reset notification before we
	 * can continue
	 */
	wait_event_interruptible(info->setup_wq, info->setup_reset_ntf);

	return r;
}

static int fdp_nci_setup(struct nci_dev *ndev)
{
	/* Format: total length followed by an NCI packet */
	struct fdp_nci_info *info = nci_get_drvdata(ndev);
	struct device *dev = &info->phy->i2c_dev->dev;
	int r;
	u8 patched = 0;

	r = nci_core_init(ndev);
	if (r)
		goto error;

	/* Get RAM and OTP version */
	r = fdp_nci_get_versions(ndev);
	if (r)
		goto error;

	/* Load firmware from disk */
	r = fdp_nci_request_firmware(ndev);
	if (r)
		goto error;

	/* Update OTP */
	if (info->otp_version < info->otp_patch_version) {
		r = fdp_nci_patch_otp(ndev);
		if (r)
			goto error;
		patched = 1;
	}

	/* Update RAM */
	if (info->ram_version < info->ram_patch_version) {
		r = fdp_nci_patch_ram(ndev);
		if (r)
			goto error;
		patched = 1;
	}

	/* Release the firmware buffers */
	fdp_nci_release_firmware(ndev);

	/* If a patch was applied the new version is checked */
	if (patched) {
		r = nci_core_init(ndev);
		if (r)
			goto error;

		r = fdp_nci_get_versions(ndev);
		if (r)
			goto error;

		if (info->otp_version != info->otp_patch_version ||
		    info->ram_version != info->ram_patch_version) {
			nfc_err(dev, "Firmware update failed");
			r = -EINVAL;
			goto error;
		}
	}

	/*
	 * We initialized the devices but the NFC subsystem expects
	 * it to not be initialized.
	 */
	return nci_core_reset(ndev);

error:
	fdp_nci_release_firmware(ndev);
	nfc_err(dev, "Setup error %d\n", r);
	return r;
}

static int fdp_nci_post_setup(struct nci_dev *ndev)
{
	struct fdp_nci_info *info = nci_get_drvdata(ndev);
	struct device *dev = &info->phy->i2c_dev->dev;
	int r;

	/* Check if the device has VSC */
	if (info->fw_vsc_cfg && info->fw_vsc_cfg[0]) {

		/* Set the vendor specific configuration */
		r = fdp_nci_set_production_data(ndev, info->fw_vsc_cfg[3],
						&info->fw_vsc_cfg[4]);
		if (r) {
			nfc_err(dev, "Vendor specific config set error %d\n",
				r);
			return r;
		}
	}

	/* Set clock type and frequency */
	r = fdp_nci_set_clock(ndev, info->clock_type, info->clock_freq);
	if (r) {
		nfc_err(dev, "Clock set error %d\n", r);
		return r;
	}

	/*
	 * In order to apply the VSC FDP needs a reset
	 */
	r = nci_core_reset(ndev);
	if (r)
		return r;

	/**
	 * The nci core was initialized when post setup was called
	 * so we leave it like that
	 */
	return nci_core_init(ndev);
}

static int fdp_nci_core_reset_ntf_packet(struct nci_dev *ndev,
					  struct sk_buff *skb)
{
	struct fdp_nci_info *info = nci_get_drvdata(ndev);

	info->setup_reset_ntf = 1;
	wake_up(&info->setup_wq);

	return 0;
}

static int fdp_nci_prop_patch_ntf_packet(struct nci_dev *ndev,
					  struct sk_buff *skb)
{
	struct fdp_nci_info *info = nci_get_drvdata(ndev);

	info->setup_patch_ntf = 1;
	info->setup_patch_status = skb->data[0];
	wake_up(&info->setup_wq);

	return 0;
}

static int fdp_nci_prop_patch_rsp_packet(struct nci_dev *ndev,
					  struct sk_buff *skb)
{
	struct fdp_nci_info *info = nci_get_drvdata(ndev);
	struct device *dev = &info->phy->i2c_dev->dev;
	u8 status = skb->data[0];

	dev_dbg(dev, "%s: status 0x%x\n", __func__, status);
	nci_req_complete(ndev, status);

	return 0;
}

static int fdp_nci_prop_set_production_data_rsp_packet(struct nci_dev *ndev,
							struct sk_buff *skb)
{
	struct fdp_nci_info *info = nci_get_drvdata(ndev);
	struct device *dev = &info->phy->i2c_dev->dev;
	u8 status = skb->data[0];

	dev_dbg(dev, "%s: status 0x%x\n", __func__, status);
	nci_req_complete(ndev, status);

	return 0;
}

static int fdp_nci_core_get_config_rsp_packet(struct nci_dev *ndev,
						struct sk_buff *skb)
{
	struct fdp_nci_info *info = nci_get_drvdata(ndev);
	struct device *dev = &info->phy->i2c_dev->dev;
	const struct nci_core_get_config_rsp *rsp = (void *) skb->data;
	unsigned int i;
	const u8 *p;

	if (rsp->status == NCI_STATUS_OK) {

		p = rsp->data;
		for (i = 0; i < 4; i++) {

			switch (*p++) {
			case NCI_PARAM_ID_FW_RAM_VERSION:
				p++;
				info->ram_version = le32_to_cpup((__le32 *) p);
				p += 4;
				break;
			case NCI_PARAM_ID_FW_OTP_VERSION:
				p++;
				info->otp_version = le32_to_cpup((__le32 *) p);
				p += 4;
				break;
			case NCI_PARAM_ID_OTP_LIMITED_VERSION:
				p++;
				info->otp_version = le32_to_cpup((__le32 *) p);
				p += 4;
				break;
			case NCI_PARAM_ID_KEY_INDEX_ID:
				p++;
				info->key_index = *p++;
			}
		}
	}

	dev_dbg(dev, "OTP version %d\n", info->otp_version);
	dev_dbg(dev, "RAM version %d\n", info->ram_version);
	dev_dbg(dev, "key index %d\n", info->key_index);
	dev_dbg(dev, "%s: status 0x%x\n", __func__, rsp->status);

	nci_req_complete(ndev, rsp->status);

	return 0;
}

static const struct nci_driver_ops fdp_core_ops[] = {
	{
		.opcode = NCI_OP_CORE_GET_CONFIG_RSP,
		.rsp = fdp_nci_core_get_config_rsp_packet,
	},
	{
		.opcode = NCI_OP_CORE_RESET_NTF,
		.ntf = fdp_nci_core_reset_ntf_packet,
	},
};

static const struct nci_driver_ops fdp_prop_ops[] = {
	{
		.opcode = nci_opcode_pack(NCI_GID_PROP, NCI_OP_PROP_PATCH_OID),
		.rsp = fdp_nci_prop_patch_rsp_packet,
		.ntf = fdp_nci_prop_patch_ntf_packet,
	},
	{
		.opcode = nci_opcode_pack(NCI_GID_PROP,
					  NCI_OP_PROP_SET_PDATA_OID),
		.rsp = fdp_nci_prop_set_production_data_rsp_packet,
	},
};

static const struct nci_ops nci_ops = {
	.open = fdp_nci_open,
	.close = fdp_nci_close,
	.send = fdp_nci_send,
	.setup = fdp_nci_setup,
	.post_setup = fdp_nci_post_setup,
	.prop_ops = fdp_prop_ops,
	.n_prop_ops = ARRAY_SIZE(fdp_prop_ops),
	.core_ops = fdp_core_ops,
	.n_core_ops = ARRAY_SIZE(fdp_core_ops),
};

int fdp_nci_probe(struct fdp_i2c_phy *phy, const struct nfc_phy_ops *phy_ops,
			struct nci_dev **ndevp, int tx_headroom,
			int tx_tailroom, u8 clock_type, u32 clock_freq,
			const u8 *fw_vsc_cfg)
{
	struct device *dev = &phy->i2c_dev->dev;
	struct fdp_nci_info *info;
	struct nci_dev *ndev;
	u32 protocols;
	int r;

	info = devm_kzalloc(dev, sizeof(struct fdp_nci_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->phy = phy;
	info->phy_ops = phy_ops;
	info->clock_type = clock_type;
	info->clock_freq = clock_freq;
	info->fw_vsc_cfg = fw_vsc_cfg;

	init_waitqueue_head(&info->setup_wq);

	protocols = NFC_PROTO_JEWEL_MASK |
		    NFC_PROTO_MIFARE_MASK |
		    NFC_PROTO_FELICA_MASK |
		    NFC_PROTO_ISO14443_MASK |
		    NFC_PROTO_ISO14443_B_MASK |
		    NFC_PROTO_NFC_DEP_MASK |
		    NFC_PROTO_ISO15693_MASK;

	BUILD_BUG_ON(ARRAY_SIZE(fdp_prop_ops) > NCI_MAX_PROPRIETARY_CMD);
	ndev = nci_allocate_device(&nci_ops, protocols, tx_headroom,
				   tx_tailroom);
	if (!ndev) {
		nfc_err(dev, "Cannot allocate nfc ndev\n");
		return -ENOMEM;
	}

	r = nci_register_device(ndev);
	if (r)
		goto err_regdev;

	*ndevp = ndev;
	info->ndev = ndev;

	nci_set_drvdata(ndev, info);

	return 0;

err_regdev:
	nci_free_device(ndev);
	return r;
}
EXPORT_SYMBOL(fdp_nci_probe);

void fdp_nci_remove(struct nci_dev *ndev)
{
	nci_unregister_device(ndev);
	nci_free_device(ndev);
}
EXPORT_SYMBOL(fdp_nci_remove);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NFC NCI driver for Intel Fields Peak NFC controller");
MODULE_AUTHOR("Robert Dolca <robert.dolca@intel.com>");

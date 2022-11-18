// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the MaxLinear MxL69x family of combo tuners/demods
 *
 * Copyright (C) 2020 Brad Love <brad@nextdimension.cc>
 *
 * based on code:
 * Copyright (c) 2016 MaxLinear, Inc. All rights reserved
 * which was released under GPL V2
 */

#include <linux/mutex.h>
#include <linux/i2c-mux.h>
#include <linux/string.h>
#include <linux/firmware.h>

#include "mxl692.h"
#include "mxl692_defs.h"

static const struct dvb_frontend_ops mxl692_ops;

struct mxl692_dev {
	struct dvb_frontend fe;
	struct i2c_client *i2c_client;
	struct mutex i2c_lock;		/* i2c command mutex */
	enum MXL_EAGLE_DEMOD_TYPE_E demod_type;
	enum MXL_EAGLE_POWER_MODE_E power_mode;
	u32 current_frequency;
	int device_type;
	int seqnum;
	int init_done;
};

static int mxl692_i2c_write(struct mxl692_dev *dev, u8 *buffer, u16 buf_len)
{
	int ret = 0;
	struct i2c_msg msg = {
		.addr = dev->i2c_client->addr,
		.flags = 0,
		.buf = buffer,
		.len = buf_len
	};

	ret = i2c_transfer(dev->i2c_client->adapter, &msg, 1);
	if (ret != 1)
		dev_dbg(&dev->i2c_client->dev, "i2c write error!\n");

	return ret;
}

static int mxl692_i2c_read(struct mxl692_dev *dev, u8 *buffer, u16 buf_len)
{
	int ret = 0;
	struct i2c_msg msg = {
		.addr = dev->i2c_client->addr,
		.flags = I2C_M_RD,
		.buf = buffer,
		.len = buf_len
	};

	ret = i2c_transfer(dev->i2c_client->adapter, &msg, 1);
	if (ret != 1)
		dev_dbg(&dev->i2c_client->dev, "i2c read error!\n");

	return ret;
}

static int convert_endian(u32 size, u8 *d)
{
	u32 i;

	for (i = 0; i < (size & ~3); i += 4) {
		d[i + 0] ^= d[i + 3];
		d[i + 3] ^= d[i + 0];
		d[i + 0] ^= d[i + 3];

		d[i + 1] ^= d[i + 2];
		d[i + 2] ^= d[i + 1];
		d[i + 1] ^= d[i + 2];
	}

	switch (size & 3) {
	case 0:
	case 1:
		/* do nothing */
		break;
	case 2:
		d[i + 0] ^= d[i + 1];
		d[i + 1] ^= d[i + 0];
		d[i + 0] ^= d[i + 1];
		break;

	case 3:
		d[i + 0] ^= d[i + 2];
		d[i + 2] ^= d[i + 0];
		d[i + 0] ^= d[i + 2];
		break;
	}
	return size;
}

static int convert_endian_n(int n, u32 size, u8 *d)
{
	int i, count = 0;

	for (i = 0; i < n; i += size)
		count += convert_endian(size, d + i);
	return count;
}

static void mxl692_tx_swap(enum MXL_EAGLE_OPCODE_E opcode, u8 *buffer)
{
#ifdef __BIG_ENDIAN
	return;
#endif
	buffer += MXL_EAGLE_HOST_MSG_HEADER_SIZE; /* skip API header */

	switch (opcode) {
	case MXL_EAGLE_OPCODE_DEVICE_INTR_MASK_SET:
	case MXL_EAGLE_OPCODE_TUNER_CHANNEL_TUNE_SET:
	case MXL_EAGLE_OPCODE_SMA_TRANSMIT_SET:
		buffer += convert_endian(sizeof(u32), buffer);
		break;
	case MXL_EAGLE_OPCODE_QAM_PARAMS_SET:
		buffer += 5;
		buffer += convert_endian(2 * sizeof(u32), buffer);
		break;
	default:
		/* no swapping - all get opcodes */
		/* ATSC/OOB no swapping */
		break;
	}
}

static void mxl692_rx_swap(enum MXL_EAGLE_OPCODE_E opcode, u8 *buffer)
{
#ifdef __BIG_ENDIAN
	return;
#endif
	buffer += MXL_EAGLE_HOST_MSG_HEADER_SIZE; /* skip API header */

	switch (opcode) {
	case MXL_EAGLE_OPCODE_TUNER_AGC_STATUS_GET:
		buffer++;
		buffer += convert_endian(2 * sizeof(u16), buffer);
		break;
	case MXL_EAGLE_OPCODE_ATSC_STATUS_GET:
		buffer += convert_endian_n(2, sizeof(u16), buffer);
		buffer += convert_endian(sizeof(u32), buffer);
		break;
	case MXL_EAGLE_OPCODE_ATSC_ERROR_COUNTERS_GET:
		buffer += convert_endian(3 * sizeof(u32), buffer);
		break;
	case MXL_EAGLE_OPCODE_ATSC_EQUALIZER_FILTER_FFE_TAPS_GET:
		buffer += convert_endian_n(24, sizeof(u16), buffer);
		break;
	case MXL_EAGLE_OPCODE_QAM_STATUS_GET:
		buffer += 8;
		buffer += convert_endian_n(2, sizeof(u16), buffer);
		buffer += convert_endian(sizeof(u32), buffer);
		break;
	case MXL_EAGLE_OPCODE_QAM_ERROR_COUNTERS_GET:
		buffer += convert_endian(7 * sizeof(u32), buffer);
		break;
	case MXL_EAGLE_OPCODE_QAM_CONSTELLATION_VALUE_GET:
	case MXL_EAGLE_OPCODE_QAM_EQUALIZER_FILTER_DFE_START_GET:
	case MXL_EAGLE_OPCODE_QAM_EQUALIZER_FILTER_DFE_MIDDLE_GET:
	case MXL_EAGLE_OPCODE_QAM_EQUALIZER_FILTER_DFE_END_GET:
	case MXL_EAGLE_OPCODE_QAM_EQUALIZER_FILTER_SPUR_START_GET:
		buffer += convert_endian_n(24, sizeof(u16), buffer);
		break;
	case MXL_EAGLE_OPCODE_QAM_EQUALIZER_FILTER_SPUR_END_GET:
		buffer += convert_endian_n(8, sizeof(u16), buffer);
		break;
	case MXL_EAGLE_OPCODE_QAM_EQUALIZER_FILTER_FFE_GET:
		buffer += convert_endian_n(17, sizeof(u16), buffer);
		break;
	case MXL_EAGLE_OPCODE_OOB_ERROR_COUNTERS_GET:
		buffer += convert_endian(3 * sizeof(u32), buffer);
		break;
	case MXL_EAGLE_OPCODE_OOB_STATUS_GET:
		buffer += convert_endian_n(2, sizeof(u16), buffer);
		buffer += convert_endian(sizeof(u32), buffer);
		break;
	case MXL_EAGLE_OPCODE_SMA_RECEIVE_GET:
		buffer += convert_endian(sizeof(u32), buffer);
		break;
	default:
		/* no swapping - all set opcodes */
		break;
	}
}

static u32 mxl692_checksum(u8 *buffer, u32 size)
{
	u32 ix, div_size;
	u32 cur_cksum = 0;
	__be32 *buf;

	div_size = DIV_ROUND_UP(size, 4);

	buf = (__be32 *)buffer;
	for (ix = 0; ix < div_size; ix++)
		cur_cksum += be32_to_cpu(buf[ix]);

	cur_cksum ^= 0xDEADBEEF;

	return cur_cksum;
}

static int mxl692_validate_fw_header(struct mxl692_dev *dev,
				     const u8 *buffer, u32 buf_len)
{
	int status = 0;
	u32 ix, temp;
	__be32 *local_buf = NULL;
	u8 temp_cksum = 0;
	static const u8 fw_hdr[] = {
		0x4D, 0x31, 0x10, 0x02, 0x40, 0x00, 0x00, 0x80
	};

	if (memcmp(buffer, fw_hdr, 8) != 0) {
		status = -EINVAL;
		goto err_finish;
	}

	local_buf = (__be32 *)(buffer + 8);
	temp = be32_to_cpu(*local_buf);

	if ((buf_len - 16) != temp >> 8) {
		status = -EINVAL;
		goto err_finish;
	}

	for (ix = 16; ix < buf_len; ix++)
		temp_cksum += buffer[ix];

	if (temp_cksum != buffer[11])
		status = -EINVAL;

err_finish:
	if (status)
		dev_dbg(&dev->i2c_client->dev, "failed\n");
	return status;
}

static int mxl692_write_fw_block(struct mxl692_dev *dev, const u8 *buffer,
				 u32 buf_len, u32 *index)
{
	int status = 0;
	u32 ix = 0, total_len = 0, addr = 0, chunk_len = 0, prevchunk_len = 0;
	u8 local_buf[MXL_EAGLE_MAX_I2C_PACKET_SIZE] = {}, *plocal_buf = NULL;
	int payload_max = MXL_EAGLE_MAX_I2C_PACKET_SIZE - MXL_EAGLE_I2C_MHEADER_SIZE;

	ix = *index;

	if (buffer[ix] == 0x53) {
		total_len = buffer[ix + 1] << 16 | buffer[ix + 2] << 8 | buffer[ix + 3];
		total_len = (total_len + 3) & ~3;
		addr      = buffer[ix + 4] << 24 | buffer[ix + 5] << 16 |
			    buffer[ix + 6] << 8 | buffer[ix + 7];
		ix       += MXL_EAGLE_FW_SEGMENT_HEADER_SIZE;

		while ((total_len > 0) && (status == 0)) {
			plocal_buf = local_buf;
			chunk_len  = (total_len < payload_max) ? total_len : payload_max;

			*plocal_buf++ = 0xFC;
			*plocal_buf++ = chunk_len + sizeof(u32);

			*(u32 *)plocal_buf = addr + prevchunk_len;
#ifdef __BIG_ENDIAN
			convert_endian(sizeof(u32), plocal_buf);
#endif
			plocal_buf += sizeof(u32);

			memcpy(plocal_buf, &buffer[ix], chunk_len);
			convert_endian(chunk_len, plocal_buf);
			if (mxl692_i2c_write(dev, local_buf,
					     (chunk_len + MXL_EAGLE_I2C_MHEADER_SIZE)) < 0) {
				status = -EREMOTEIO;
				break;
			}

			prevchunk_len += chunk_len;
			total_len -= chunk_len;
			ix += chunk_len;
		}
		*index = ix;
	} else {
		status = -EINVAL;
	}

	if (status)
		dev_dbg(&dev->i2c_client->dev, "err %d\n", status);

	return status;
}

static int mxl692_memwrite(struct mxl692_dev *dev, u32 addr,
			   u8 *buffer, u32 size)
{
	int status = 0, total_len = 0;
	u8 local_buf[MXL_EAGLE_MAX_I2C_PACKET_SIZE] = {}, *plocal_buf = NULL;

	total_len = size;
	total_len = (total_len + 3) & ~3;  /* 4 byte alignment */

	if (total_len > (MXL_EAGLE_MAX_I2C_PACKET_SIZE - MXL_EAGLE_I2C_MHEADER_SIZE))
		dev_dbg(&dev->i2c_client->dev, "hrmph?\n");

	plocal_buf = local_buf;

	*plocal_buf++ = 0xFC;
	*plocal_buf++ = total_len + sizeof(u32);

	*(u32 *)plocal_buf = addr;
	plocal_buf += sizeof(u32);

	memcpy(plocal_buf, buffer, total_len);
#ifdef __BIG_ENDIAN
	convert_endian(sizeof(u32) + total_len, local_buf + 2);
#endif
	if (mxl692_i2c_write(dev, local_buf,
			     (total_len + MXL_EAGLE_I2C_MHEADER_SIZE)) < 0) {
		status = -EREMOTEIO;
		goto err_finish;
	}

	return status;
err_finish:
	dev_dbg(&dev->i2c_client->dev, "err %d\n", status);
	return status;
}

static int mxl692_memread(struct mxl692_dev *dev, u32 addr,
			  u8 *buffer, u32 size)
{
	int status = 0;
	u8 local_buf[MXL_EAGLE_I2C_MHEADER_SIZE] = {}, *plocal_buf = NULL;

	plocal_buf = local_buf;

	*plocal_buf++ = 0xFB;
	*plocal_buf++ = sizeof(u32);
	*(u32 *)plocal_buf = addr;
#ifdef __BIG_ENDIAN
	convert_endian(sizeof(u32), plocal_buf);
#endif
	mutex_lock(&dev->i2c_lock);

	if (mxl692_i2c_write(dev, local_buf, MXL_EAGLE_I2C_MHEADER_SIZE) > 0) {
		size = (size + 3) & ~3;  /* 4 byte alignment */
		status = mxl692_i2c_read(dev, buffer, (u16)size) < 0 ? -EREMOTEIO : 0;
#ifdef __BIG_ENDIAN
		if (status == 0)
			convert_endian(size, buffer);
#endif
	} else {
		status = -EREMOTEIO;
	}

	mutex_unlock(&dev->i2c_lock);

	if (status)
		dev_dbg(&dev->i2c_client->dev, "err %d\n", status);

	return status;
}

static const char *mxl692_opcode_string(u8 opcode)
{
	if (opcode <= MXL_EAGLE_OPCODE_INTERNAL)
		return MXL_EAGLE_OPCODE_STRING[opcode];

	return "invalid opcode";
}

static int mxl692_opwrite(struct mxl692_dev *dev, u8 *buffer,
			  u32 size)
{
	int status = 0, total_len = 0;
	u8 local_buf[MXL_EAGLE_MAX_I2C_PACKET_SIZE] = {}, *plocal_buf = NULL;
	struct MXL_EAGLE_HOST_MSG_HEADER_T *tx_hdr = (struct MXL_EAGLE_HOST_MSG_HEADER_T *)buffer;

	total_len = size;
	total_len = (total_len + 3) & ~3;  /* 4 byte alignment */

	if (total_len > (MXL_EAGLE_MAX_I2C_PACKET_SIZE - MXL_EAGLE_I2C_PHEADER_SIZE))
		dev_dbg(&dev->i2c_client->dev, "hrmph?\n");

	plocal_buf = local_buf;

	*plocal_buf++ = 0xFE;
	*plocal_buf++ = (u8)total_len;

	memcpy(plocal_buf, buffer, total_len);
	convert_endian(total_len, plocal_buf);

	if (mxl692_i2c_write(dev, local_buf,
			     (total_len + MXL_EAGLE_I2C_PHEADER_SIZE)) < 0) {
		status = -EREMOTEIO;
		goto err_finish;
	}
err_finish:
	if (status)
		dev_dbg(&dev->i2c_client->dev, "opcode %s  err %d\n",
			mxl692_opcode_string(tx_hdr->opcode), status);
	return status;
}

static int mxl692_opread(struct mxl692_dev *dev, u8 *buffer,
			 u32 size)
{
	int status = 0;
	u32 ix = 0;
	u8 local_buf[MXL_EAGLE_I2C_PHEADER_SIZE] = {};

	local_buf[0] = 0xFD;
	local_buf[1] = 0;

	if (mxl692_i2c_write(dev, local_buf, MXL_EAGLE_I2C_PHEADER_SIZE) > 0) {
		size = (size + 3) & ~3;  /* 4 byte alignment */

		/* Read in 4 byte chunks */
		for (ix = 0; ix < size; ix += 4) {
			if (mxl692_i2c_read(dev, buffer + ix, 4) < 0) {
				dev_dbg(&dev->i2c_client->dev, "ix=%d   size=%d\n", ix, size);
				status = -EREMOTEIO;
				goto err_finish;
			}
		}
		convert_endian(size, buffer);
	} else {
		status = -EREMOTEIO;
	}
err_finish:
	if (status)
		dev_dbg(&dev->i2c_client->dev, "err %d\n", status);
	return status;
}

static int mxl692_i2c_writeread(struct mxl692_dev *dev,
				u8 opcode,
				u8 *tx_payload,
				u8 tx_payload_size,
				u8 *rx_payload,
				u8 rx_payload_expected)
{
	int status = 0, timeout = 40;
	u8 tx_buf[MXL_EAGLE_MAX_I2C_PACKET_SIZE] = {};
	u8 rx_buf[MXL_EAGLE_MAX_I2C_PACKET_SIZE] = {};
	u32 resp_checksum = 0, resp_checksum_tmp = 0;
	struct MXL_EAGLE_HOST_MSG_HEADER_T *tx_header;
	struct MXL_EAGLE_HOST_MSG_HEADER_T *rx_header;

	mutex_lock(&dev->i2c_lock);

	if ((tx_payload_size + MXL_EAGLE_HOST_MSG_HEADER_SIZE) >
	    (MXL_EAGLE_MAX_I2C_PACKET_SIZE - MXL_EAGLE_I2C_PHEADER_SIZE)) {
		status = -EINVAL;
		goto err_finish;
	}

	tx_header = (struct MXL_EAGLE_HOST_MSG_HEADER_T *)tx_buf;
	tx_header->opcode = opcode;
	tx_header->seqnum = dev->seqnum++;
	tx_header->payload_size = tx_payload_size;
	tx_header->checksum = 0;

	if (dev->seqnum == 0)
		dev->seqnum = 1;

	if (tx_payload && tx_payload_size > 0)
		memcpy(&tx_buf[MXL_EAGLE_HOST_MSG_HEADER_SIZE], tx_payload, tx_payload_size);

	mxl692_tx_swap(opcode, tx_buf);

	tx_header->checksum = 0;
	tx_header->checksum = mxl692_checksum(tx_buf,
					      MXL_EAGLE_HOST_MSG_HEADER_SIZE + tx_payload_size);
#ifdef __LITTLE_ENDIAN
	convert_endian(4, (u8 *)&tx_header->checksum); /* cksum is big endian */
#endif
	/* send Tx message */
	status = mxl692_opwrite(dev, tx_buf,
				tx_payload_size + MXL_EAGLE_HOST_MSG_HEADER_SIZE);
	if (status) {
		status = -EREMOTEIO;
		goto err_finish;
	}

	/* receive Rx message (polling) */
	rx_header = (struct MXL_EAGLE_HOST_MSG_HEADER_T *)rx_buf;

	do {
		status = mxl692_opread(dev, rx_buf,
				       rx_payload_expected + MXL_EAGLE_HOST_MSG_HEADER_SIZE);
		usleep_range(1000, 2000);
		timeout--;
	} while ((timeout > 0) && (status == 0) &&
		 (rx_header->seqnum == 0) &&
		 (rx_header->checksum == 0));

	if (timeout == 0 || status) {
		dev_dbg(&dev->i2c_client->dev, "timeout=%d   status=%d\n",
			timeout, status);
		status = -ETIMEDOUT;
		goto err_finish;
	}

	if (rx_header->status) {
		dev_dbg(&dev->i2c_client->dev, "rx header status code: %d\n", rx_header->status);
		status = -EREMOTEIO;
		goto err_finish;
	}

	if (rx_header->seqnum != tx_header->seqnum ||
	    rx_header->opcode != tx_header->opcode ||
	    rx_header->payload_size != rx_payload_expected) {
		dev_dbg(&dev->i2c_client->dev, "Something failed seq=%s  opcode=%s  pSize=%s\n",
			rx_header->seqnum != tx_header->seqnum ? "X" : "0",
			rx_header->opcode != tx_header->opcode ? "X" : "0",
			rx_header->payload_size != rx_payload_expected ? "X" : "0");
		if (rx_header->payload_size != rx_payload_expected)
			dev_dbg(&dev->i2c_client->dev,
				"rx_header->payloadSize=%d   rx_payload_expected=%d\n",
				rx_header->payload_size, rx_payload_expected);
		status = -EREMOTEIO;
		goto err_finish;
	}

	resp_checksum = rx_header->checksum;
	rx_header->checksum = 0;

	resp_checksum_tmp = mxl692_checksum(rx_buf,
					    MXL_EAGLE_HOST_MSG_HEADER_SIZE + rx_header->payload_size);
#ifdef __LITTLE_ENDIAN
	convert_endian(4, (u8 *)&resp_checksum_tmp); /* cksum is big endian */
#endif
	if (resp_checksum != resp_checksum_tmp) {
		dev_dbg(&dev->i2c_client->dev, "rx checksum failure\n");
		status = -EREMOTEIO;
		goto err_finish;
	}

	mxl692_rx_swap(rx_header->opcode, rx_buf);

	if (rx_header->payload_size > 0) {
		if (!rx_payload) {
			dev_dbg(&dev->i2c_client->dev, "no rx payload?!?\n");
			status = -EREMOTEIO;
			goto err_finish;
		}
		memcpy(rx_payload, rx_buf + MXL_EAGLE_HOST_MSG_HEADER_SIZE,
		       rx_header->payload_size);
	}
err_finish:
	if (status)
		dev_dbg(&dev->i2c_client->dev, "err %d\n", status);

	mutex_unlock(&dev->i2c_lock);
	return status;
}

static int mxl692_fwdownload(struct mxl692_dev *dev,
			     const u8 *firmware_buf, u32 buf_len)
{
	int status = 0;
	u32 ix, reg_val = 0x1;
	u8 rx_buf[MXL_EAGLE_MAX_I2C_PACKET_SIZE] = {};
	struct MXL_EAGLE_DEV_STATUS_T *dev_status;

	if (buf_len < MXL_EAGLE_FW_HEADER_SIZE ||
	    buf_len > MXL_EAGLE_FW_MAX_SIZE_IN_KB * 1000)
		return -EINVAL;

	mutex_lock(&dev->i2c_lock);

	dev_dbg(&dev->i2c_client->dev, "\n");

	status = mxl692_validate_fw_header(dev, firmware_buf, buf_len);
	if (status)
		goto err_finish;

	ix = 16;
	status = mxl692_write_fw_block(dev, firmware_buf, buf_len, &ix); /* DRAM */
	if (status)
		goto err_finish;

	status = mxl692_write_fw_block(dev, firmware_buf, buf_len, &ix); /* IRAM */
	if (status)
		goto err_finish;

	/* release CPU from reset */
	status = mxl692_memwrite(dev, 0x70000018, (u8 *)&reg_val, sizeof(u32));
	if (status)
		goto err_finish;

	mutex_unlock(&dev->i2c_lock);

	if (status == 0) {
		/* verify FW is alive */
		usleep_range(MXL_EAGLE_FW_LOAD_TIME * 1000, (MXL_EAGLE_FW_LOAD_TIME + 5) * 1000);
		dev_status = (struct MXL_EAGLE_DEV_STATUS_T *)&rx_buf;
		status = mxl692_i2c_writeread(dev,
					      MXL_EAGLE_OPCODE_DEVICE_STATUS_GET,
					      NULL,
					      0,
					      (u8 *)dev_status,
					      sizeof(struct MXL_EAGLE_DEV_STATUS_T));
	}

	return status;
err_finish:
	mutex_unlock(&dev->i2c_lock);
	if (status)
		dev_dbg(&dev->i2c_client->dev, "err %d\n", status);
	return status;
}

static int mxl692_get_versions(struct mxl692_dev *dev)
{
	int status = 0;
	struct MXL_EAGLE_DEV_VER_T dev_ver = {};
	static const char * const chip_id[] = {"N/A", "691", "248", "692"};

	status = mxl692_i2c_writeread(dev, MXL_EAGLE_OPCODE_DEVICE_VERSION_GET,
				      NULL,
				      0,
				      (u8 *)&dev_ver,
				      sizeof(struct MXL_EAGLE_DEV_VER_T));
	if (status)
		return status;

	dev_info(&dev->i2c_client->dev, "MxL692_DEMOD Chip ID: %s\n",
		 chip_id[dev_ver.chip_id]);

	dev_info(&dev->i2c_client->dev,
		 "MxL692_DEMOD FW Version: %d.%d.%d.%d_RC%d\n",
		 dev_ver.firmware_ver[0],
		 dev_ver.firmware_ver[1],
		 dev_ver.firmware_ver[2],
		 dev_ver.firmware_ver[3],
		 dev_ver.firmware_ver[4]);

	return status;
}

static int mxl692_reset(struct mxl692_dev *dev)
{
	int status = 0;
	u32 dev_type = MXL_EAGLE_DEVICE_MAX, reg_val = 0x2;

	dev_dbg(&dev->i2c_client->dev, "\n");

	/* legacy i2c override */
	status = mxl692_memwrite(dev, 0x80000100, (u8 *)&reg_val, sizeof(u32));
	if (status)
		goto err_finish;

	/* verify sku */
	status = mxl692_memread(dev, 0x70000188, (u8 *)&dev_type, sizeof(u32));
	if (status)
		goto err_finish;

	if (dev_type != dev->device_type)
		goto err_finish;

err_finish:
	if (status)
		dev_dbg(&dev->i2c_client->dev, "err %d\n", status);
	return status;
}

static int mxl692_config_regulators(struct mxl692_dev *dev,
				    enum MXL_EAGLE_POWER_SUPPLY_SOURCE_E power_supply)
{
	int status = 0;
	u32 reg_val;

	dev_dbg(&dev->i2c_client->dev, "\n");

	/* configure main regulator according to the power supply source */
	status = mxl692_memread(dev, 0x90000000, (u8 *)&reg_val, sizeof(u32));
	if (status)
		goto err_finish;

	reg_val &= 0x00FFFFFF;
	reg_val |= (power_supply == MXL_EAGLE_POWER_SUPPLY_SOURCE_SINGLE) ?
					0x14000000 : 0x10000000;

	status = mxl692_memwrite(dev, 0x90000000, (u8 *)&reg_val, sizeof(u32));
	if (status)
		goto err_finish;

	/* configure digital regulator to high current mode */
	status = mxl692_memread(dev, 0x90000018, (u8 *)&reg_val, sizeof(u32));
	if (status)
		goto err_finish;

	reg_val |= 0x800;

	status = mxl692_memwrite(dev, 0x90000018, (u8 *)&reg_val, sizeof(u32));

err_finish:
	if (status)
		dev_dbg(&dev->i2c_client->dev, "err %d\n", status);
	return status;
}

static int mxl692_config_xtal(struct mxl692_dev *dev,
			      struct MXL_EAGLE_DEV_XTAL_T *dev_xtal)
{
	int status = 0;
	u32 reg_val, reg_val1;

	dev_dbg(&dev->i2c_client->dev, "\n");

	status = mxl692_memread(dev, 0x90000000, (u8 *)&reg_val, sizeof(u32));
	if (status)
		goto err_finish;

	/* set XTAL capacitance */
	reg_val &= 0xFFFFFFE0;
	reg_val |= dev_xtal->xtal_cap;

	/* set CLK OUT */
	reg_val = dev_xtal->clk_out_enable ? (reg_val | 0x0100) : (reg_val & 0xFFFFFEFF);

	status = mxl692_memwrite(dev, 0x90000000, (u8 *)&reg_val, sizeof(u32));
	if (status)
		goto err_finish;

	/* set CLK OUT divider */
	reg_val = dev_xtal->clk_out_div_enable ? (reg_val | 0x0200) : (reg_val & 0xFFFFFDFF);

	status = mxl692_memwrite(dev, 0x90000000, (u8 *)&reg_val, sizeof(u32));
	if (status)
		goto err_finish;

	/* set XTAL sharing */
	reg_val = dev_xtal->xtal_sharing_enable ? (reg_val | 0x010400) : (reg_val & 0xFFFEFBFF);

	status = mxl692_memwrite(dev, 0x90000000, (u8 *)&reg_val, sizeof(u32));
	if (status)
		goto err_finish;

	/* enable/disable XTAL calibration, based on master/slave device */
	status = mxl692_memread(dev, 0x90000030, (u8 *)&reg_val1, sizeof(u32));
	if (status)
		goto err_finish;

	if (dev_xtal->xtal_calibration_enable) {
		/* enable XTAL calibration and set XTAL amplitude to a higher value */
		reg_val1 &= 0xFFFFFFFD;
		reg_val1 |= 0x30;

		status = mxl692_memwrite(dev, 0x90000030, (u8 *)&reg_val1, sizeof(u32));
		if (status)
			goto err_finish;
	} else {
		/* disable XTAL calibration */
		reg_val1 |= 0x2;

		status = mxl692_memwrite(dev, 0x90000030, (u8 *)&reg_val1, sizeof(u32));
		if (status)
			goto err_finish;

		/* set XTAL bias value */
		status = mxl692_memread(dev, 0x9000002c, (u8 *)&reg_val, sizeof(u32));
		if (status)
			goto err_finish;

		reg_val &= 0xC0FFFFFF;
		reg_val |= 0xA000000;

		status = mxl692_memwrite(dev, 0x9000002c, (u8 *)&reg_val, sizeof(u32));
		if (status)
			goto err_finish;
	}

	/* start XTAL calibration */
	status = mxl692_memread(dev, 0x70000010, (u8 *)&reg_val, sizeof(u32));
	if (status)
		goto err_finish;

	reg_val |= 0x8;

	status = mxl692_memwrite(dev, 0x70000010, (u8 *)&reg_val, sizeof(u32));
	if (status)
		goto err_finish;

	status = mxl692_memread(dev, 0x70000018, (u8 *)&reg_val, sizeof(u32));
	if (status)
		goto err_finish;

	reg_val |= 0x10;

	status = mxl692_memwrite(dev, 0x70000018, (u8 *)&reg_val, sizeof(u32));
	if (status)
		goto err_finish;

	status = mxl692_memread(dev, 0x9001014c, (u8 *)&reg_val, sizeof(u32));
	if (status)
		goto err_finish;

	reg_val &= 0xFFFFEFFF;

	status = mxl692_memwrite(dev, 0x9001014c, (u8 *)&reg_val, sizeof(u32));
	if (status)
		goto err_finish;

	reg_val |= 0x1000;

	status = mxl692_memwrite(dev, 0x9001014c, (u8 *)&reg_val, sizeof(u32));
	if (status)
		goto err_finish;

	usleep_range(45000, 55000);

err_finish:
	if (status)
		dev_dbg(&dev->i2c_client->dev, "err %d\n", status);
	return status;
}

static int mxl692_powermode(struct mxl692_dev *dev,
			    enum MXL_EAGLE_POWER_MODE_E power_mode)
{
	int status = 0;
	u8 mode = power_mode;

	dev_dbg(&dev->i2c_client->dev, "%s\n",
		power_mode == MXL_EAGLE_POWER_MODE_SLEEP ? "sleep" : "active");

	status = mxl692_i2c_writeread(dev,
				      MXL_EAGLE_OPCODE_DEVICE_POWERMODE_SET,
				      &mode,
				      sizeof(u8),
				      NULL,
				      0);
	if (status) {
		dev_dbg(&dev->i2c_client->dev, "err %d\n", status);
		return status;
	}

	dev->power_mode = power_mode;

	return status;
}

static int mxl692_init(struct dvb_frontend *fe)
{
	struct mxl692_dev *dev = fe->demodulator_priv;
	struct i2c_client *client = dev->i2c_client;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int status = 0;
	const struct firmware *firmware;
	struct MXL_EAGLE_DEV_XTAL_T xtal_config = {};

	dev_dbg(&dev->i2c_client->dev, "\n");

	if (dev->init_done)
		goto warm;

	dev->seqnum = 1;

	status = mxl692_reset(dev);
	if (status)
		goto err;

	usleep_range(50 * 1000, 60 * 1000); /* was 1000! */

	status = mxl692_config_regulators(dev, MXL_EAGLE_POWER_SUPPLY_SOURCE_DUAL);
	if (status)
		goto err;

	xtal_config.xtal_cap = 26;
	xtal_config.clk_out_div_enable = 0;
	xtal_config.clk_out_enable = 0;
	xtal_config.xtal_calibration_enable = 0;
	xtal_config.xtal_sharing_enable = 1;
	status = mxl692_config_xtal(dev, &xtal_config);
	if (status)
		goto err;

	status = request_firmware(&firmware, MXL692_FIRMWARE, &client->dev);
	if (status) {
		dev_dbg(&dev->i2c_client->dev, "firmware missing? %s\n",
			MXL692_FIRMWARE);
		goto err;
	}

	status = mxl692_fwdownload(dev, firmware->data, firmware->size);
	if (status)
		goto err_release_firmware;

	release_firmware(firmware);

	status = mxl692_get_versions(dev);
	if (status)
		goto err;

	dev->power_mode = MXL_EAGLE_POWER_MODE_SLEEP;
warm:
	/* Config Device Power Mode */
	if (dev->power_mode != MXL_EAGLE_POWER_MODE_ACTIVE) {
		status = mxl692_powermode(dev, MXL_EAGLE_POWER_MODE_ACTIVE);
		if (status)
			goto err;

		usleep_range(50 * 1000, 60 * 1000); /* was 500! */
	}

	/* Init stats here to indicate which stats are supported */
	c->cnr.len = 1;
	c->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->post_bit_error.len = 1;
	c->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->post_bit_count.len = 1;
	c->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->block_error.len = 1;
	c->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

	dev->init_done = 1;
	return 0;
err_release_firmware:
	release_firmware(firmware);
err:
	dev_dbg(&dev->i2c_client->dev, "err %d\n", status);
	return status;
}

static int mxl692_sleep(struct dvb_frontend *fe)
{
	struct mxl692_dev *dev = fe->demodulator_priv;

	if (dev->power_mode != MXL_EAGLE_POWER_MODE_SLEEP)
		mxl692_powermode(dev, MXL_EAGLE_POWER_MODE_SLEEP);

	return 0;
}

static int mxl692_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct mxl692_dev *dev = fe->demodulator_priv;

	int status = 0;
	enum MXL_EAGLE_DEMOD_TYPE_E demod_type;
	struct MXL_EAGLE_MPEGOUT_PARAMS_T mpeg_params = {};
	enum MXL_EAGLE_QAM_DEMOD_ANNEX_TYPE_E qam_annex = MXL_EAGLE_QAM_DEMOD_ANNEX_B;
	struct MXL_EAGLE_QAM_DEMOD_PARAMS_T qam_params = {};
	struct MXL_EAGLE_TUNER_CHANNEL_PARAMS_T tuner_params = {};
	u8 op_param = 0;

	dev_dbg(&dev->i2c_client->dev, "\n");

	switch (p->modulation) {
	case VSB_8:
		demod_type = MXL_EAGLE_DEMOD_TYPE_ATSC;
		break;
	case QAM_AUTO:
	case QAM_64:
	case QAM_128:
	case QAM_256:
		demod_type = MXL_EAGLE_DEMOD_TYPE_QAM;
		break;
	default:
		return -EINVAL;
	}

	if (dev->current_frequency == p->frequency && dev->demod_type == demod_type) {
		dev_dbg(&dev->i2c_client->dev, "already set up\n");
		return 0;
	}

	dev->current_frequency = -1;
	dev->demod_type = -1;

	op_param = demod_type;
	status = mxl692_i2c_writeread(dev,
				      MXL_EAGLE_OPCODE_DEVICE_DEMODULATOR_TYPE_SET,
				      &op_param,
				      sizeof(u8),
				      NULL,
				      0);
	if (status) {
		dev_dbg(&dev->i2c_client->dev,
			"DEVICE_DEMODULATOR_TYPE_SET...FAIL  err 0x%x\n", status);
		goto err;
	}

	usleep_range(20 * 1000, 30 * 1000); /* was 500! */

	mpeg_params.mpeg_parallel = 0;
	mpeg_params.msb_first = MXL_EAGLE_DATA_SERIAL_MSB_1ST;
	mpeg_params.mpeg_sync_pulse_width = MXL_EAGLE_DATA_SYNC_WIDTH_BIT;
	mpeg_params.mpeg_valid_pol = MXL_EAGLE_CLOCK_POSITIVE;
	mpeg_params.mpeg_sync_pol = MXL_EAGLE_CLOCK_POSITIVE;
	mpeg_params.mpeg_clk_pol = MXL_EAGLE_CLOCK_NEGATIVE;
	mpeg_params.mpeg3wire_mode_enable = 0;
	mpeg_params.mpeg_clk_freq = MXL_EAGLE_MPEG_CLOCK_27MHZ;

	switch (demod_type) {
	case MXL_EAGLE_DEMOD_TYPE_ATSC:
		status = mxl692_i2c_writeread(dev,
					      MXL_EAGLE_OPCODE_DEVICE_MPEG_OUT_PARAMS_SET,
					      (u8 *)&mpeg_params,
					      sizeof(struct MXL_EAGLE_MPEGOUT_PARAMS_T),
					      NULL,
					      0);
		if (status)
			goto err;
		break;
	case MXL_EAGLE_DEMOD_TYPE_QAM:
		if (qam_annex == MXL_EAGLE_QAM_DEMOD_ANNEX_A)
			mpeg_params.msb_first = MXL_EAGLE_DATA_SERIAL_LSB_1ST;
		status = mxl692_i2c_writeread(dev,
					      MXL_EAGLE_OPCODE_DEVICE_MPEG_OUT_PARAMS_SET,
					      (u8 *)&mpeg_params,
					      sizeof(struct MXL_EAGLE_MPEGOUT_PARAMS_T),
					      NULL,
					      0);
		if (status)
			goto err;

		qam_params.annex_type = qam_annex;
		qam_params.qam_type = MXL_EAGLE_QAM_DEMOD_AUTO;
		qam_params.iq_flip = MXL_EAGLE_DEMOD_IQ_AUTO;
		if (p->modulation == QAM_64)
			qam_params.symbol_rate_hz = 5057000;
		else
			qam_params.symbol_rate_hz = 5361000;

		qam_params.symbol_rate_256qam_hz = 5361000;

		status = mxl692_i2c_writeread(dev,
					      MXL_EAGLE_OPCODE_QAM_PARAMS_SET,
					      (u8 *)&qam_params,
					      sizeof(struct MXL_EAGLE_QAM_DEMOD_PARAMS_T),
					      NULL, 0);
		if (status)
			goto err;

		break;
	default:
		break;
	}

	usleep_range(20 * 1000, 30 * 1000); /* was 500! */

	tuner_params.freq_hz = p->frequency;
	tuner_params.bandwidth = MXL_EAGLE_TUNER_BW_6MHZ;
	tuner_params.tune_mode = MXL_EAGLE_TUNER_CHANNEL_TUNE_MODE_VIEW;

	dev_dbg(&dev->i2c_client->dev, " Tuning Freq: %d %s\n", tuner_params.freq_hz,
		demod_type == MXL_EAGLE_DEMOD_TYPE_ATSC ? "ATSC" : "QAM");

	status = mxl692_i2c_writeread(dev,
				      MXL_EAGLE_OPCODE_TUNER_CHANNEL_TUNE_SET,
				      (u8 *)&tuner_params,
				      sizeof(struct MXL_EAGLE_TUNER_CHANNEL_PARAMS_T),
				      NULL,
				      0);
	if (status)
		goto err;

	usleep_range(20 * 1000, 30 * 1000); /* was 500! */

	switch (demod_type) {
	case MXL_EAGLE_DEMOD_TYPE_ATSC:
		status = mxl692_i2c_writeread(dev,
					      MXL_EAGLE_OPCODE_ATSC_INIT_SET,
					      NULL, 0, NULL, 0);
		if (status)
			goto err;
		break;
	case MXL_EAGLE_DEMOD_TYPE_QAM:
		status = mxl692_i2c_writeread(dev,
					      MXL_EAGLE_OPCODE_QAM_RESTART_SET,
					      NULL, 0, NULL, 0);
		if (status)
			goto err;
		break;
	default:
		break;
	}

	dev->demod_type = demod_type;
	dev->current_frequency = p->frequency;

	return 0;
err:
	dev_dbg(&dev->i2c_client->dev, "err %d\n", status);
	return status;
}

static int mxl692_get_frontend(struct dvb_frontend *fe,
			       struct dtv_frontend_properties *p)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	p->modulation = c->modulation;
	p->frequency = c->frequency;

	return 0;
}

static int mxl692_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct mxl692_dev *dev = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u8 rx_buf[MXL_EAGLE_MAX_I2C_PACKET_SIZE] = {};
	struct MXL_EAGLE_ATSC_DEMOD_STATUS_T *atsc_status;
	struct MXL_EAGLE_QAM_DEMOD_STATUS_T *qam_status;
	enum MXL_EAGLE_DEMOD_TYPE_E demod_type = dev->demod_type;
	int mxl_status = 0;

	*snr = 0;

	dev_dbg(&dev->i2c_client->dev, "\n");

	atsc_status = (struct MXL_EAGLE_ATSC_DEMOD_STATUS_T *)&rx_buf;
	qam_status = (struct MXL_EAGLE_QAM_DEMOD_STATUS_T *)&rx_buf;

	switch (demod_type) {
	case MXL_EAGLE_DEMOD_TYPE_ATSC:
		mxl_status = mxl692_i2c_writeread(dev,
						  MXL_EAGLE_OPCODE_ATSC_STATUS_GET,
						  NULL,
						  0,
						  rx_buf,
						  sizeof(struct MXL_EAGLE_ATSC_DEMOD_STATUS_T));
		if (!mxl_status) {
			*snr = (u16)(atsc_status->snr_db_tenths / 10);
			c->cnr.stat[0].scale = FE_SCALE_DECIBEL;
			c->cnr.stat[0].svalue = *snr;
		}
		break;
	case MXL_EAGLE_DEMOD_TYPE_QAM:
		mxl_status = mxl692_i2c_writeread(dev,
						  MXL_EAGLE_OPCODE_QAM_STATUS_GET,
						  NULL,
						  0,
						  rx_buf,
						  sizeof(struct MXL_EAGLE_QAM_DEMOD_STATUS_T));
		if (!mxl_status)
			*snr = (u16)(qam_status->snr_db_tenths / 10);
		break;
	case MXL_EAGLE_DEMOD_TYPE_OOB:
	default:
		break;
	}

	if (mxl_status)
		dev_dbg(&dev->i2c_client->dev, "err %d\n", mxl_status);
	return mxl_status;
}

static int mxl692_read_ber_ucb(struct dvb_frontend *fe)
{
	struct mxl692_dev *dev = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u8 rx_buf[MXL_EAGLE_MAX_I2C_PACKET_SIZE] = {};
	struct MXL_EAGLE_ATSC_DEMOD_ERROR_COUNTERS_T *atsc_errors;
	enum MXL_EAGLE_DEMOD_TYPE_E demod_type = dev->demod_type;
	int mxl_status = 0;
	u32 utmp;

	dev_dbg(&dev->i2c_client->dev, "\n");

	atsc_errors = (struct MXL_EAGLE_ATSC_DEMOD_ERROR_COUNTERS_T *)&rx_buf;

	switch (demod_type) {
	case MXL_EAGLE_DEMOD_TYPE_ATSC:
		mxl_status = mxl692_i2c_writeread(dev,
						  MXL_EAGLE_OPCODE_ATSC_ERROR_COUNTERS_GET,
						  NULL,
						  0,
						  rx_buf,
						  sizeof(struct MXL_EAGLE_ATSC_DEMOD_ERROR_COUNTERS_T));
		if (!mxl_status) {
			if (atsc_errors->error_packets == 0)
				utmp = 0;
			else
				utmp = ((atsc_errors->error_bytes / atsc_errors->error_packets) *
					atsc_errors->total_packets);
			/* ber */
			c->post_bit_error.stat[0].scale = FE_SCALE_COUNTER;
			c->post_bit_error.stat[0].uvalue += atsc_errors->error_bytes;
			c->post_bit_count.stat[0].scale = FE_SCALE_COUNTER;
			c->post_bit_count.stat[0].uvalue += utmp;
			/* ucb */
			c->block_error.stat[0].scale = FE_SCALE_COUNTER;
			c->block_error.stat[0].uvalue += atsc_errors->error_packets;

			dev_dbg(&dev->i2c_client->dev, "%llu   %llu\n",
				c->post_bit_count.stat[0].uvalue, c->block_error.stat[0].uvalue);
		}
		break;
	case MXL_EAGLE_DEMOD_TYPE_QAM:
	case MXL_EAGLE_DEMOD_TYPE_OOB:
	default:
		break;
	}

	if (mxl_status)
		dev_dbg(&dev->i2c_client->dev, "err %d\n", mxl_status);

	return mxl_status;
}

static int mxl692_read_status(struct dvb_frontend *fe,
			      enum fe_status *status)
{
	struct mxl692_dev *dev = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u8 rx_buf[MXL_EAGLE_MAX_I2C_PACKET_SIZE] = {};
	struct MXL_EAGLE_ATSC_DEMOD_STATUS_T *atsc_status;
	struct MXL_EAGLE_QAM_DEMOD_STATUS_T *qam_status;
	enum MXL_EAGLE_DEMOD_TYPE_E demod_type = dev->demod_type;
	int mxl_status = 0;
	*status = 0;

	dev_dbg(&dev->i2c_client->dev, "\n");

	atsc_status = (struct MXL_EAGLE_ATSC_DEMOD_STATUS_T *)&rx_buf;
	qam_status = (struct MXL_EAGLE_QAM_DEMOD_STATUS_T *)&rx_buf;

	switch (demod_type) {
	case MXL_EAGLE_DEMOD_TYPE_ATSC:
		mxl_status = mxl692_i2c_writeread(dev,
						  MXL_EAGLE_OPCODE_ATSC_STATUS_GET,
						  NULL,
						  0,
						  rx_buf,
						  sizeof(struct MXL_EAGLE_ATSC_DEMOD_STATUS_T));
		if (!mxl_status && atsc_status->atsc_lock) {
			*status |= FE_HAS_SIGNAL;
			*status |= FE_HAS_CARRIER;
			*status |= FE_HAS_VITERBI;
			*status |= FE_HAS_SYNC;
			*status |= FE_HAS_LOCK;

			c->cnr.stat[0].scale = FE_SCALE_DECIBEL;
			c->cnr.stat[0].svalue = atsc_status->snr_db_tenths / 10;
		}
		break;
	case MXL_EAGLE_DEMOD_TYPE_QAM:
		mxl_status = mxl692_i2c_writeread(dev,
						  MXL_EAGLE_OPCODE_QAM_STATUS_GET,
						  NULL,
						  0,
						  rx_buf,
						  sizeof(struct MXL_EAGLE_QAM_DEMOD_STATUS_T));
		if (!mxl_status && qam_status->qam_locked) {
			*status |= FE_HAS_SIGNAL;
			*status |= FE_HAS_CARRIER;
			*status |= FE_HAS_VITERBI;
			*status |= FE_HAS_SYNC;
			*status |= FE_HAS_LOCK;

			c->cnr.stat[0].scale = FE_SCALE_DECIBEL;
			c->cnr.stat[0].svalue = qam_status->snr_db_tenths / 10;
		}
		break;
	case MXL_EAGLE_DEMOD_TYPE_OOB:
	default:
		break;
	}

	if ((*status & FE_HAS_LOCK) == 0) {
		/* No lock, reset all statistics */
		c->cnr.len = 1;
		c->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		return 0;
	}

	if (mxl_status)
		dev_dbg(&dev->i2c_client->dev, "err %d\n", mxl_status);
	else
		mxl_status = mxl692_read_ber_ucb(fe);

	return mxl_status;
}

static const struct dvb_frontend_ops mxl692_ops = {
	.delsys = { SYS_ATSC },
	.info = {
		.name = "MaxLinear MxL692 VSB tuner-demodulator",
		.frequency_min_hz      = 54000000,
		.frequency_max_hz      = 858000000,
		.frequency_stepsize_hz = 62500,
		.caps = FE_CAN_8VSB
	},

	.init         = mxl692_init,
	.sleep        = mxl692_sleep,
	.set_frontend = mxl692_set_frontend,
	.get_frontend = mxl692_get_frontend,

	.read_status          = mxl692_read_status,
	.read_snr             = mxl692_read_snr,
};

static int mxl692_probe(struct i2c_client *client)
{
	struct mxl692_config *config = client->dev.platform_data;
	struct mxl692_dev *dev;
	int ret = 0;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		dev_dbg(&client->dev, "kzalloc() failed\n");
		goto err;
	}

	memcpy(&dev->fe.ops, &mxl692_ops, sizeof(struct dvb_frontend_ops));
	dev->fe.demodulator_priv = dev;
	dev->i2c_client = client;
	*config->fe = &dev->fe;
	mutex_init(&dev->i2c_lock);
	i2c_set_clientdata(client, dev);

	dev_info(&client->dev, "MaxLinear mxl692 successfully attached\n");

	return 0;
err:
	dev_dbg(&client->dev, "failed %d\n", ret);
	return -ENODEV;
}

static void mxl692_remove(struct i2c_client *client)
{
	struct mxl692_dev *dev = i2c_get_clientdata(client);

	dev->fe.demodulator_priv = NULL;
	i2c_set_clientdata(client, NULL);
	kfree(dev);
}

static const struct i2c_device_id mxl692_id_table[] = {
	{"mxl692", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, mxl692_id_table);

static struct i2c_driver mxl692_driver = {
	.driver = {
		.name	= "mxl692",
	},
	.probe_new	= mxl692_probe,
	.remove		= mxl692_remove,
	.id_table	= mxl692_id_table,
};

module_i2c_driver(mxl692_driver);

MODULE_AUTHOR("Brad Love <brad@nextdimension.cc>");
MODULE_DESCRIPTION("MaxLinear MxL692 demodulator/tuner driver");
MODULE_FIRMWARE(MXL692_FIRMWARE);
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Sensor Protocol
 *
 * Copyright (C) 2018 ARM Ltd.
 */

#include "common.h"

enum scmi_sensor_protocol_cmd {
	SENSOR_DESCRIPTION_GET = 0x3,
	SENSOR_TRIP_POINT_NOTIFY = 0x4,
	SENSOR_TRIP_POINT_CONFIG = 0x5,
	SENSOR_READING_GET = 0x6,
};

struct scmi_msg_resp_sensor_attributes {
	__le16 num_sensors;
	u8 max_requests;
	u8 reserved;
	__le32 reg_addr_low;
	__le32 reg_addr_high;
	__le32 reg_size;
};

struct scmi_msg_resp_sensor_description {
	__le16 num_returned;
	__le16 num_remaining;
	struct {
		__le32 id;
		__le32 attributes_low;
#define SUPPORTS_ASYNC_READ(x)	((x) & BIT(31))
#define NUM_TRIP_POINTS(x)	((x) & 0xff)
		__le32 attributes_high;
#define SENSOR_TYPE(x)		((x) & 0xff)
#define SENSOR_SCALE(x)		(((x) >> 11) & 0x1f)
#define SENSOR_SCALE_SIGN	BIT(4)
#define SENSOR_SCALE_EXTEND	GENMASK(7, 5)
#define SENSOR_UPDATE_SCALE(x)	(((x) >> 22) & 0x1f)
#define SENSOR_UPDATE_BASE(x)	(((x) >> 27) & 0x1f)
		    u8 name[SCMI_MAX_STR_SIZE];
	} desc[0];
};

struct scmi_msg_sensor_trip_point_notify {
	__le32 id;
	__le32 event_control;
#define SENSOR_TP_NOTIFY_ALL	BIT(0)
};

struct scmi_msg_set_sensor_trip_point {
	__le32 id;
	__le32 event_control;
#define SENSOR_TP_EVENT_MASK	(0x3)
#define SENSOR_TP_DISABLED	0x0
#define SENSOR_TP_POSITIVE	0x1
#define SENSOR_TP_NEGATIVE	0x2
#define SENSOR_TP_BOTH		0x3
#define SENSOR_TP_ID(x)		(((x) & 0xff) << 4)
	__le32 value_low;
	__le32 value_high;
};

struct scmi_msg_sensor_reading_get {
	__le32 id;
	__le32 flags;
#define SENSOR_READ_ASYNC	BIT(0)
};

struct sensors_info {
	u32 version;
	int num_sensors;
	int max_requests;
	u64 reg_addr;
	u32 reg_size;
	struct scmi_sensor_info *sensors;
};

static int scmi_sensor_attributes_get(const struct scmi_handle *handle,
				      struct sensors_info *si)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_resp_sensor_attributes *attr;

	ret = scmi_xfer_get_init(handle, PROTOCOL_ATTRIBUTES,
				 SCMI_PROTOCOL_SENSOR, 0, sizeof(*attr), &t);
	if (ret)
		return ret;

	attr = t->rx.buf;

	ret = scmi_do_xfer(handle, t);
	if (!ret) {
		si->num_sensors = le16_to_cpu(attr->num_sensors);
		si->max_requests = attr->max_requests;
		si->reg_addr = le32_to_cpu(attr->reg_addr_low) |
				(u64)le32_to_cpu(attr->reg_addr_high) << 32;
		si->reg_size = le32_to_cpu(attr->reg_size);
	}

	scmi_xfer_put(handle, t);
	return ret;
}

static int scmi_sensor_description_get(const struct scmi_handle *handle,
				       struct sensors_info *si)
{
	int ret, cnt;
	u32 desc_index = 0;
	u16 num_returned, num_remaining;
	struct scmi_xfer *t;
	struct scmi_msg_resp_sensor_description *buf;

	ret = scmi_xfer_get_init(handle, SENSOR_DESCRIPTION_GET,
				 SCMI_PROTOCOL_SENSOR, sizeof(__le32), 0, &t);
	if (ret)
		return ret;

	buf = t->rx.buf;

	do {
		/* Set the number of sensors to be skipped/already read */
		put_unaligned_le32(desc_index, t->tx.buf);

		ret = scmi_do_xfer(handle, t);
		if (ret)
			break;

		num_returned = le16_to_cpu(buf->num_returned);
		num_remaining = le16_to_cpu(buf->num_remaining);

		if (desc_index + num_returned > si->num_sensors) {
			dev_err(handle->dev, "No. of sensors can't exceed %d",
				si->num_sensors);
			break;
		}

		for (cnt = 0; cnt < num_returned; cnt++) {
			u32 attrh, attrl;
			struct scmi_sensor_info *s;

			attrl = le32_to_cpu(buf->desc[cnt].attributes_low);
			attrh = le32_to_cpu(buf->desc[cnt].attributes_high);
			s = &si->sensors[desc_index + cnt];
			s->id = le32_to_cpu(buf->desc[cnt].id);
			s->type = SENSOR_TYPE(attrh);
			s->scale = SENSOR_SCALE(attrh);
			/* Sign extend to a full s8 */
			if (s->scale & SENSOR_SCALE_SIGN)
				s->scale |= SENSOR_SCALE_EXTEND;
			s->async = SUPPORTS_ASYNC_READ(attrl);
			s->num_trip_points = NUM_TRIP_POINTS(attrl);
			strlcpy(s->name, buf->desc[cnt].name, SCMI_MAX_STR_SIZE);
		}

		desc_index += num_returned;
		/*
		 * check for both returned and remaining to avoid infinite
		 * loop due to buggy firmware
		 */
	} while (num_returned && num_remaining);

	scmi_xfer_put(handle, t);
	return ret;
}

static int scmi_sensor_trip_point_notify(const struct scmi_handle *handle,
					 u32 sensor_id, bool enable)
{
	int ret;
	u32 evt_cntl = enable ? SENSOR_TP_NOTIFY_ALL : 0;
	struct scmi_xfer *t;
	struct scmi_msg_sensor_trip_point_notify *cfg;

	ret = scmi_xfer_get_init(handle, SENSOR_TRIP_POINT_NOTIFY,
				 SCMI_PROTOCOL_SENSOR, sizeof(*cfg), 0, &t);
	if (ret)
		return ret;

	cfg = t->tx.buf;
	cfg->id = cpu_to_le32(sensor_id);
	cfg->event_control = cpu_to_le32(evt_cntl);

	ret = scmi_do_xfer(handle, t);

	scmi_xfer_put(handle, t);
	return ret;
}

static int
scmi_sensor_trip_point_config(const struct scmi_handle *handle, u32 sensor_id,
			      u8 trip_id, u64 trip_value)
{
	int ret;
	u32 evt_cntl = SENSOR_TP_BOTH;
	struct scmi_xfer *t;
	struct scmi_msg_set_sensor_trip_point *trip;

	ret = scmi_xfer_get_init(handle, SENSOR_TRIP_POINT_CONFIG,
				 SCMI_PROTOCOL_SENSOR, sizeof(*trip), 0, &t);
	if (ret)
		return ret;

	trip = t->tx.buf;
	trip->id = cpu_to_le32(sensor_id);
	trip->event_control = cpu_to_le32(evt_cntl | SENSOR_TP_ID(trip_id));
	trip->value_low = cpu_to_le32(trip_value & 0xffffffff);
	trip->value_high = cpu_to_le32(trip_value >> 32);

	ret = scmi_do_xfer(handle, t);

	scmi_xfer_put(handle, t);
	return ret;
}

static int scmi_sensor_reading_get(const struct scmi_handle *handle,
				   u32 sensor_id, u64 *value)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_sensor_reading_get *sensor;
	struct sensors_info *si = handle->sensor_priv;
	struct scmi_sensor_info *s = si->sensors + sensor_id;

	ret = scmi_xfer_get_init(handle, SENSOR_READING_GET,
				 SCMI_PROTOCOL_SENSOR, sizeof(*sensor),
				 sizeof(u64), &t);
	if (ret)
		return ret;

	sensor = t->tx.buf;
	sensor->id = cpu_to_le32(sensor_id);

	if (s->async) {
		sensor->flags = cpu_to_le32(SENSOR_READ_ASYNC);
		ret = scmi_do_xfer_with_response(handle, t);
		if (!ret)
			*value = get_unaligned_le64((void *)
						    ((__le32 *)t->rx.buf + 1));
	} else {
		sensor->flags = cpu_to_le32(0);
		ret = scmi_do_xfer(handle, t);
		if (!ret)
			*value = get_unaligned_le64(t->rx.buf);
	}

	scmi_xfer_put(handle, t);
	return ret;
}

static const struct scmi_sensor_info *
scmi_sensor_info_get(const struct scmi_handle *handle, u32 sensor_id)
{
	struct sensors_info *si = handle->sensor_priv;

	return si->sensors + sensor_id;
}

static int scmi_sensor_count_get(const struct scmi_handle *handle)
{
	struct sensors_info *si = handle->sensor_priv;

	return si->num_sensors;
}

static struct scmi_sensor_ops sensor_ops = {
	.count_get = scmi_sensor_count_get,
	.info_get = scmi_sensor_info_get,
	.trip_point_notify = scmi_sensor_trip_point_notify,
	.trip_point_config = scmi_sensor_trip_point_config,
	.reading_get = scmi_sensor_reading_get,
};

static int scmi_sensors_protocol_init(struct scmi_handle *handle)
{
	u32 version;
	struct sensors_info *sinfo;

	scmi_version_get(handle, SCMI_PROTOCOL_SENSOR, &version);

	dev_dbg(handle->dev, "Sensor Version %d.%d\n",
		PROTOCOL_REV_MAJOR(version), PROTOCOL_REV_MINOR(version));

	sinfo = devm_kzalloc(handle->dev, sizeof(*sinfo), GFP_KERNEL);
	if (!sinfo)
		return -ENOMEM;

	scmi_sensor_attributes_get(handle, sinfo);

	sinfo->sensors = devm_kcalloc(handle->dev, sinfo->num_sensors,
				      sizeof(*sinfo->sensors), GFP_KERNEL);
	if (!sinfo->sensors)
		return -ENOMEM;

	scmi_sensor_description_get(handle, sinfo);

	sinfo->version = version;
	handle->sensor_ops = &sensor_ops;
	handle->sensor_priv = sinfo;

	return 0;
}

static int __init scmi_sensors_init(void)
{
	return scmi_protocol_register(SCMI_PROTOCOL_SENSOR,
				      &scmi_sensors_protocol_init);
}
subsys_initcall(scmi_sensors_init);

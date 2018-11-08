// SPDX-License-Identifier: GPL-2.0

#include <linux/device.h>
#include <linux/hwmon-sysfs.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/mutex.h>
#include <asm/unaligned.h>

#include "common.h"

#define EXTN_FLAG_SENSOR_ID		BIT(7)

#define OCC_UPDATE_FREQUENCY		msecs_to_jiffies(1000)

#define OCC_TEMP_SENSOR_FAULT		0xFF

#define OCC_FRU_TYPE_VRM		3

/* OCC sensor type and version definitions */

struct temp_sensor_1 {
	u16 sensor_id;
	u16 value;
} __packed;

struct temp_sensor_2 {
	u32 sensor_id;
	u8 fru_type;
	u8 value;
} __packed;

struct freq_sensor_1 {
	u16 sensor_id;
	u16 value;
} __packed;

struct freq_sensor_2 {
	u32 sensor_id;
	u16 value;
} __packed;

struct power_sensor_1 {
	u16 sensor_id;
	u32 update_tag;
	u32 accumulator;
	u16 value;
} __packed;

struct power_sensor_2 {
	u32 sensor_id;
	u8 function_id;
	u8 apss_channel;
	u16 reserved;
	u32 update_tag;
	u64 accumulator;
	u16 value;
} __packed;

struct power_sensor_data {
	u16 value;
	u32 update_tag;
	u64 accumulator;
} __packed;

struct power_sensor_data_and_time {
	u16 update_time;
	u16 value;
	u32 update_tag;
	u64 accumulator;
} __packed;

struct power_sensor_a0 {
	u32 sensor_id;
	struct power_sensor_data_and_time system;
	u32 reserved;
	struct power_sensor_data_and_time proc;
	struct power_sensor_data vdd;
	struct power_sensor_data vdn;
} __packed;

struct caps_sensor_2 {
	u16 cap;
	u16 system_power;
	u16 n_cap;
	u16 max;
	u16 min;
	u16 user;
	u8 user_source;
} __packed;

struct caps_sensor_3 {
	u16 cap;
	u16 system_power;
	u16 n_cap;
	u16 max;
	u16 hard_min;
	u16 soft_min;
	u16 user;
	u8 user_source;
} __packed;

struct extended_sensor {
	union {
		u8 name[4];
		u32 sensor_id;
	};
	u8 flags;
	u8 reserved;
	u8 data[6];
} __packed;

static int occ_poll(struct occ *occ)
{
	u16 checksum = occ->poll_cmd_data + 1;
	u8 cmd[8];

	/* big endian */
	cmd[0] = 0;			/* sequence number */
	cmd[1] = 0;			/* cmd type */
	cmd[2] = 0;			/* data length msb */
	cmd[3] = 1;			/* data length lsb */
	cmd[4] = occ->poll_cmd_data;	/* data */
	cmd[5] = checksum >> 8;		/* checksum msb */
	cmd[6] = checksum & 0xFF;	/* checksum lsb */
	cmd[7] = 0;

	/* mutex should already be locked if necessary */
	return occ->send_cmd(occ, cmd);
}

static int occ_set_user_power_cap(struct occ *occ, u16 user_power_cap)
{
	int rc;
	u8 cmd[8];
	u16 checksum = 0x24;
	__be16 user_power_cap_be = cpu_to_be16(user_power_cap);

	cmd[0] = 0;
	cmd[1] = 0x22;
	cmd[2] = 0;
	cmd[3] = 2;

	memcpy(&cmd[4], &user_power_cap_be, 2);

	checksum += cmd[4] + cmd[5];
	cmd[6] = checksum >> 8;
	cmd[7] = checksum & 0xFF;

	rc = mutex_lock_interruptible(&occ->lock);
	if (rc)
		return rc;

	rc = occ->send_cmd(occ, cmd);

	mutex_unlock(&occ->lock);

	return rc;
}

static int occ_update_response(struct occ *occ)
{
	int rc = mutex_lock_interruptible(&occ->lock);

	if (rc)
		return rc;

	/* limit the maximum rate of polling the OCC */
	if (time_after(jiffies, occ->last_update + OCC_UPDATE_FREQUENCY)) {
		rc = occ_poll(occ);
		occ->last_update = jiffies;
	}

	mutex_unlock(&occ->lock);
	return rc;
}

static ssize_t occ_show_temp_1(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int rc;
	u32 val = 0;
	struct temp_sensor_1 *temp;
	struct occ *occ = dev_get_drvdata(dev);
	struct occ_sensors *sensors = &occ->sensors;
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);

	rc = occ_update_response(occ);
	if (rc)
		return rc;

	temp = ((struct temp_sensor_1 *)sensors->temp.data) + sattr->index;

	switch (sattr->nr) {
	case 0:
		val = get_unaligned_be16(&temp->sensor_id);
		break;
	case 1:
		val = get_unaligned_be16(&temp->value) * 1000;
		break;
	default:
		return -EINVAL;
	}

	return snprintf(buf, PAGE_SIZE - 1, "%u\n", val);
}

static ssize_t occ_show_temp_2(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int rc;
	u32 val = 0;
	struct temp_sensor_2 *temp;
	struct occ *occ = dev_get_drvdata(dev);
	struct occ_sensors *sensors = &occ->sensors;
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);

	rc = occ_update_response(occ);
	if (rc)
		return rc;

	temp = ((struct temp_sensor_2 *)sensors->temp.data) + sattr->index;

	switch (sattr->nr) {
	case 0:
		val = get_unaligned_be32(&temp->sensor_id);
		break;
	case 1:
		val = temp->value;
		if (val == OCC_TEMP_SENSOR_FAULT)
			return -EREMOTEIO;

		/*
		 * VRM doesn't return temperature, only alarm bit. This
		 * attribute maps to tempX_alarm instead of tempX_input for
		 * VRM
		 */
		if (temp->fru_type != OCC_FRU_TYPE_VRM) {
			/* sensor not ready */
			if (val == 0)
				return -EAGAIN;

			val *= 1000;
		}
		break;
	case 2:
		val = temp->fru_type;
		break;
	case 3:
		val = temp->value == OCC_TEMP_SENSOR_FAULT;
		break;
	default:
		return -EINVAL;
	}

	return snprintf(buf, PAGE_SIZE - 1, "%u\n", val);
}

static ssize_t occ_show_freq_1(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int rc;
	u16 val = 0;
	struct freq_sensor_1 *freq;
	struct occ *occ = dev_get_drvdata(dev);
	struct occ_sensors *sensors = &occ->sensors;
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);

	rc = occ_update_response(occ);
	if (rc)
		return rc;

	freq = ((struct freq_sensor_1 *)sensors->freq.data) + sattr->index;

	switch (sattr->nr) {
	case 0:
		val = get_unaligned_be16(&freq->sensor_id);
		break;
	case 1:
		val = get_unaligned_be16(&freq->value);
		break;
	default:
		return -EINVAL;
	}

	return snprintf(buf, PAGE_SIZE - 1, "%u\n", val);
}

static ssize_t occ_show_freq_2(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int rc;
	u32 val = 0;
	struct freq_sensor_2 *freq;
	struct occ *occ = dev_get_drvdata(dev);
	struct occ_sensors *sensors = &occ->sensors;
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);

	rc = occ_update_response(occ);
	if (rc)
		return rc;

	freq = ((struct freq_sensor_2 *)sensors->freq.data) + sattr->index;

	switch (sattr->nr) {
	case 0:
		val = get_unaligned_be32(&freq->sensor_id);
		break;
	case 1:
		val = get_unaligned_be16(&freq->value);
		break;
	default:
		return -EINVAL;
	}

	return snprintf(buf, PAGE_SIZE - 1, "%u\n", val);
}

static ssize_t occ_show_power_1(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int rc;
	u64 val = 0;
	struct power_sensor_1 *power;
	struct occ *occ = dev_get_drvdata(dev);
	struct occ_sensors *sensors = &occ->sensors;
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);

	rc = occ_update_response(occ);
	if (rc)
		return rc;

	power = ((struct power_sensor_1 *)sensors->power.data) + sattr->index;

	switch (sattr->nr) {
	case 0:
		val = get_unaligned_be16(&power->sensor_id);
		break;
	case 1:
		val = get_unaligned_be32(&power->accumulator) /
			get_unaligned_be32(&power->update_tag);
		val *= 1000000ULL;
		break;
	case 2:
		val = get_unaligned_be32(&power->update_tag) *
			occ->powr_sample_time_us;
		break;
	case 3:
		val = get_unaligned_be16(&power->value) * 1000000ULL;
		break;
	default:
		return -EINVAL;
	}

	return snprintf(buf, PAGE_SIZE - 1, "%llu\n", val);
}

static u64 occ_get_powr_avg(u64 *accum, u32 *samples)
{
	return div64_u64(get_unaligned_be64(accum) * 1000000ULL,
			 get_unaligned_be32(samples));
}

static ssize_t occ_show_power_2(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int rc;
	u64 val = 0;
	struct power_sensor_2 *power;
	struct occ *occ = dev_get_drvdata(dev);
	struct occ_sensors *sensors = &occ->sensors;
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);

	rc = occ_update_response(occ);
	if (rc)
		return rc;

	power = ((struct power_sensor_2 *)sensors->power.data) + sattr->index;

	switch (sattr->nr) {
	case 0:
		return snprintf(buf, PAGE_SIZE - 1, "%u_%u_%u\n",
				get_unaligned_be32(&power->sensor_id),
				power->function_id, power->apss_channel);
	case 1:
		val = occ_get_powr_avg(&power->accumulator,
				       &power->update_tag);
		break;
	case 2:
		val = get_unaligned_be32(&power->update_tag) *
			occ->powr_sample_time_us;
		break;
	case 3:
		val = get_unaligned_be16(&power->value) * 1000000ULL;
		break;
	default:
		return -EINVAL;
	}

	return snprintf(buf, PAGE_SIZE - 1, "%llu\n", val);
}

static ssize_t occ_show_power_a0(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int rc;
	u64 val = 0;
	struct power_sensor_a0 *power;
	struct occ *occ = dev_get_drvdata(dev);
	struct occ_sensors *sensors = &occ->sensors;
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);

	rc = occ_update_response(occ);
	if (rc)
		return rc;

	power = ((struct power_sensor_a0 *)sensors->power.data) + sattr->index;

	switch (sattr->nr) {
	case 0:
		return snprintf(buf, PAGE_SIZE - 1, "%u_system\n",
				get_unaligned_be32(&power->sensor_id));
	case 1:
		val = occ_get_powr_avg(&power->system.accumulator,
				       &power->system.update_tag);
		break;
	case 2:
		val = get_unaligned_be32(&power->system.update_tag) *
			occ->powr_sample_time_us;
		break;
	case 3:
		val = get_unaligned_be16(&power->system.value) * 1000000ULL;
		break;
	case 4:
		return snprintf(buf, PAGE_SIZE - 1, "%u_proc\n",
				get_unaligned_be32(&power->sensor_id));
	case 5:
		val = occ_get_powr_avg(&power->proc.accumulator,
				       &power->proc.update_tag);
		break;
	case 6:
		val = get_unaligned_be32(&power->proc.update_tag) *
			occ->powr_sample_time_us;
		break;
	case 7:
		val = get_unaligned_be16(&power->proc.value) * 1000000ULL;
		break;
	case 8:
		return snprintf(buf, PAGE_SIZE - 1, "%u_vdd\n",
				get_unaligned_be32(&power->sensor_id));
	case 9:
		val = occ_get_powr_avg(&power->vdd.accumulator,
				       &power->vdd.update_tag);
		break;
	case 10:
		val = get_unaligned_be32(&power->vdd.update_tag) *
			occ->powr_sample_time_us;
		break;
	case 11:
		val = get_unaligned_be16(&power->vdd.value) * 1000000ULL;
		break;
	case 12:
		return snprintf(buf, PAGE_SIZE - 1, "%u_vdn\n",
				get_unaligned_be32(&power->sensor_id));
	case 13:
		val = occ_get_powr_avg(&power->vdn.accumulator,
				       &power->vdn.update_tag);
		break;
	case 14:
		val = get_unaligned_be32(&power->vdn.update_tag) *
			occ->powr_sample_time_us;
		break;
	case 15:
		val = get_unaligned_be16(&power->vdn.value) * 1000000ULL;
		break;
	default:
		return -EINVAL;
	}

	return snprintf(buf, PAGE_SIZE - 1, "%llu\n", val);
}

static ssize_t occ_show_caps_1_2(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int rc;
	u64 val = 0;
	struct caps_sensor_2 *caps;
	struct occ *occ = dev_get_drvdata(dev);
	struct occ_sensors *sensors = &occ->sensors;
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);

	rc = occ_update_response(occ);
	if (rc)
		return rc;

	caps = ((struct caps_sensor_2 *)sensors->caps.data) + sattr->index;

	switch (sattr->nr) {
	case 0:
		return snprintf(buf, PAGE_SIZE - 1, "system\n");
	case 1:
		val = get_unaligned_be16(&caps->cap) * 1000000ULL;
		break;
	case 2:
		val = get_unaligned_be16(&caps->system_power) * 1000000ULL;
		break;
	case 3:
		val = get_unaligned_be16(&caps->n_cap) * 1000000ULL;
		break;
	case 4:
		val = get_unaligned_be16(&caps->max) * 1000000ULL;
		break;
	case 5:
		val = get_unaligned_be16(&caps->min) * 1000000ULL;
		break;
	case 6:
		val = get_unaligned_be16(&caps->user) * 1000000ULL;
		break;
	case 7:
		if (occ->sensors.caps.version == 1)
			return -EINVAL;

		val = caps->user_source;
		break;
	default:
		return -EINVAL;
	}

	return snprintf(buf, PAGE_SIZE - 1, "%llu\n", val);
}

static ssize_t occ_show_caps_3(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int rc;
	u64 val = 0;
	struct caps_sensor_3 *caps;
	struct occ *occ = dev_get_drvdata(dev);
	struct occ_sensors *sensors = &occ->sensors;
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);

	rc = occ_update_response(occ);
	if (rc)
		return rc;

	caps = ((struct caps_sensor_3 *)sensors->caps.data) + sattr->index;

	switch (sattr->nr) {
	case 0:
		return snprintf(buf, PAGE_SIZE - 1, "system\n");
	case 1:
		val = get_unaligned_be16(&caps->cap) * 1000000ULL;
		break;
	case 2:
		val = get_unaligned_be16(&caps->system_power) * 1000000ULL;
		break;
	case 3:
		val = get_unaligned_be16(&caps->n_cap) * 1000000ULL;
		break;
	case 4:
		val = get_unaligned_be16(&caps->max) * 1000000ULL;
		break;
	case 5:
		val = get_unaligned_be16(&caps->hard_min) * 1000000ULL;
		break;
	case 6:
		val = get_unaligned_be16(&caps->user) * 1000000ULL;
		break;
	case 7:
		val = caps->user_source;
		break;
	default:
		return -EINVAL;
	}

	return snprintf(buf, PAGE_SIZE - 1, "%llu\n", val);
}

static ssize_t occ_store_caps_user(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int rc;
	u16 user_power_cap;
	unsigned long long value;
	struct occ *occ = dev_get_drvdata(dev);

	rc = kstrtoull(buf, 0, &value);
	if (rc)
		return rc;

	user_power_cap = div64_u64(value, 1000000ULL); /* microwatt to watt */

	rc = occ_set_user_power_cap(occ, user_power_cap);
	if (rc)
		return rc;

	return count;
}

static ssize_t occ_show_extended(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int rc;
	struct extended_sensor *extn;
	struct occ *occ = dev_get_drvdata(dev);
	struct occ_sensors *sensors = &occ->sensors;
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);

	rc = occ_update_response(occ);
	if (rc)
		return rc;

	extn = ((struct extended_sensor *)sensors->extended.data) +
		sattr->index;

	switch (sattr->nr) {
	case 0:
		if (extn->flags & EXTN_FLAG_SENSOR_ID)
			rc = snprintf(buf, PAGE_SIZE - 1, "%u",
				      get_unaligned_be32(&extn->sensor_id));
		else
			rc = snprintf(buf, PAGE_SIZE - 1, "%02x%02x%02x%02x\n",
				      extn->name[0], extn->name[1],
				      extn->name[2], extn->name[3]);
		break;
	case 1:
		rc = snprintf(buf, PAGE_SIZE - 1, "%02x\n", extn->flags);
		break;
	case 2:
		rc = snprintf(buf, PAGE_SIZE - 1, "%02x%02x%02x%02x%02x%02x\n",
			      extn->data[0], extn->data[1], extn->data[2],
			      extn->data[3], extn->data[4], extn->data[5]);
		break;
	default:
		return -EINVAL;
	}

	return rc;
}

/* only need to do this once at startup, as OCC won't change sensors on us */
static void occ_parse_poll_response(struct occ *occ)
{
	unsigned int i, old_offset, offset = 0, size = 0;
	struct occ_sensor *sensor;
	struct occ_sensors *sensors = &occ->sensors;
	struct occ_response *resp = &occ->resp;
	struct occ_poll_response *poll =
		(struct occ_poll_response *)&resp->data[0];
	struct occ_poll_response_header *header = &poll->header;
	struct occ_sensor_data_block *block = &poll->block;

	dev_info(occ->bus_dev, "OCC found, code level: %.16s\n",
		 header->occ_code_level);

	for (i = 0; i < header->num_sensor_data_blocks; ++i) {
		block = (struct occ_sensor_data_block *)((u8 *)block + offset);
		old_offset = offset;
		offset = (block->header.num_sensors *
			  block->header.sensor_length) + sizeof(block->header);
		size += offset;

		/* validate all the length/size fields */
		if ((size + sizeof(*header)) >= OCC_RESP_DATA_BYTES) {
			dev_warn(occ->bus_dev, "exceeded response buffer\n");
			return;
		}

		dev_dbg(occ->bus_dev, " %04x..%04x: %.4s (%d sensors)\n",
			old_offset, offset - 1, block->header.eye_catcher,
			block->header.num_sensors);

		/* match sensor block type */
		if (strncmp(block->header.eye_catcher, "TEMP", 4) == 0)
			sensor = &sensors->temp;
		else if (strncmp(block->header.eye_catcher, "FREQ", 4) == 0)
			sensor = &sensors->freq;
		else if (strncmp(block->header.eye_catcher, "POWR", 4) == 0)
			sensor = &sensors->power;
		else if (strncmp(block->header.eye_catcher, "CAPS", 4) == 0)
			sensor = &sensors->caps;
		else if (strncmp(block->header.eye_catcher, "EXTN", 4) == 0)
			sensor = &sensors->extended;
		else {
			dev_warn(occ->bus_dev, "sensor not supported %.4s\n",
				 block->header.eye_catcher);
			continue;
		}

		sensor->num_sensors = block->header.num_sensors;
		sensor->version = block->header.sensor_format;
		sensor->data = &block->data;
	}

	dev_dbg(occ->bus_dev, "Max resp size: %u+%zd=%zd\n", size,
		sizeof(*header), size + sizeof(*header));
}

int occ_setup(struct occ *occ, const char *name)
{
	int rc;

	mutex_init(&occ->lock);

	/* no need to lock */
	rc = occ_poll(occ);
	if (rc == -ESHUTDOWN) {
		dev_info(occ->bus_dev, "host is not ready\n");
		return rc;
	} else if (rc < 0) {
		dev_err(occ->bus_dev, "failed to get OCC poll response: %d\n",
			rc);
		return rc;
	}

	occ_parse_poll_response(occ);

	return 0;
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/cdev.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include <linux/soc/qcom/qcom_aoss.h>
#include <linux/soc/qcom/smem.h>
#include <soc/qcom/qcom_stats.h>
#include <clocksource/arm_arch_timer.h>

#define RPM_DYNAMIC_ADDR	0x14
#define RPM_DYNAMIC_ADDR_MASK	0xFFFF

#define STAT_TYPE_OFFSET	0x0
#define COUNT_OFFSET		0x4
#define LAST_ENTERED_AT_OFFSET	0x8
#define LAST_EXITED_AT_OFFSET	0x10
#define ACCUMULATED_OFFSET	0x18
#define CLIENT_VOTES_OFFSET	0x20

#define DDR_STATS_MAGIC_KEY	0xA1157A75
#define DDR_STATS_MAX_NUM_MODES	0x14
#define MAX_DRV			28
#define MAX_MSG_LEN		64
#define DRV_ABSENT		0xdeaddead
#define DRV_INVALID		0xffffdead
#define VOTE_MASK		0x3fff
#define VOTE_X_SHIFT		14

#define DDR_STATS_MAGIC_KEY_ADDR	0x0
#define DDR_STATS_NUM_MODES_ADDR	0x4
#define DDR_STATS_ENTRY_ADDR		0x8
#define DDR_STATS_NAME_ADDR		0x0
#define DDR_STATS_COUNT_ADDR		0x4
#define DDR_STATS_DURATION_ADDR		0x8

#define MAX_ISLAND_STATS_NAME_LENGTH	16
#define MAX_ISLAND_STATS		6
#define ISLAND_STATS_PID		2 /* ADSP PID */
#define ISLAND_STATS_SMEM_ID		653

#define STATS_BASEMINOR				0
#define STATS_MAX_MINOR				1
#define STATS_DEVICE_NAME			"stats"
#define SUBSYSTEM_STATS_MAGIC_NUM		(0x9d)
#define SUBSYSTEM_STATS_OTHERS_NUM		(-2)

#define APSS_IOCTL		_IOR(SUBSYSTEM_STATS_MAGIC_NUM, 0, \
				     struct sleep_stats *)
#define MODEM_IOCTL		_IOR(SUBSYSTEM_STATS_MAGIC_NUM, 1, \
				     struct sleep_stats *)
#define WPSS_IOCTL		_IOR(SUBSYSTEM_STATS_MAGIC_NUM, 2, \
				     struct sleep_stats *)
#define ADSP_IOCTL		_IOR(SUBSYSTEM_STATS_MAGIC_NUM, 3, \
				     struct sleep_stats *)
#define ADSP_ISLAND_IOCTL	_IOR(SUBSYSTEM_STATS_MAGIC_NUM, 4, \
				     struct sleep_stats *)
#define CDSP_IOCTL		_IOR(SUBSYSTEM_STATS_MAGIC_NUM, 5, \
				     struct sleep_stats *)
#define SLPI_IOCTL		_IOR(SUBSYSTEM_STATS_MAGIC_NUM, 6, \
				     struct sleep_stats *)
#define GPU_IOCTL		_IOR(SUBSYSTEM_STATS_MAGIC_NUM, 7, \
				     struct sleep_stats *)
#define DISPLAY_IOCTL		_IOR(SUBSYSTEM_STATS_MAGIC_NUM, 8, \
				     struct sleep_stats *)
#define SLPI_ISLAND_IOCTL	_IOR(SUBSYSTEM_STATS_MAGIC_NUM, 9, \
				     struct sleep_stats *)

#define AOSD_IOCTL		_IOR(SUBSYSTEM_STATS_MAGIC_NUM, 10, \
				     struct sleep_stats *)

#define CXSD_IOCTL		_IOR(SUBSYSTEM_STATS_MAGIC_NUM, 11, \
				     struct sleep_stats *)

#define DDR_IOCTL		_IOR(SUBSYSTEM_STATS_MAGIC_NUM, 12, \
				     struct sleep_stats *)

#define DDR_STATS_IOCTL		_IOR(SUBSYSTEM_STATS_MAGIC_NUM, 13, \
				     struct sleep_stats *)

enum subsystem_smem_id {
	AOSD = 0,
	CXSD = 1,
	DDR = 2,
	DDR_STATS = 3,
	MPSS = 605,
	ADSP,
	CDSP,
	SLPI,
	GPU,
	DISPLAY,
	SLPI_ISLAND = 613,
	APSS = 631,
};

struct subsystem_data {
	const char *name;
	u32 smem_item;
	u32 pid;
	bool not_present;
};

static struct subsystem_data subsystems[] = {
	{ "modem", 605, 1 },
	{ "wpss", 605, 13 },
	{ "adsp", 606, 2 },
	{ "cdsp", 607, 5 },
	{ "cdsp1", 607, 12 },
	{ "gpdsp0", 607, 17 },
	{ "gpdsp1", 607, 18 },
	{ "slpi", 608, 3 },
	{ "gpu", 609, 0 },
	{ "display", 610, 0 },
	{ "adsp_island", 613, 2 },
	{ "slpi_island", 613, 3 },
	{ "apss", 631, QCOM_SMEM_HOST_ANY },
};

struct stats_config {
	size_t stats_offset;
	size_t ddr_stats_offset;
	size_t cx_vote_offset;
	size_t num_records;
	bool appended_stats_avail;
	bool dynamic_offset;
	bool subsystem_stats_in_smem;
	bool read_ddr_votes;
	bool ddr_freq_update;
	bool island_stats_avail;
};

struct stats_data {
	bool appended_stats_avail;
	void __iomem *base;
};

struct stats_drvdata {
	void __iomem *base;
	const struct stats_config *config;
	struct stats_data *d;
	struct dentry *root;
	dev_t		dev_no;
	struct class	*stats_class;
	struct device	*stats_device;
	struct cdev	stats_cdev;
	struct mutex lock;
	struct qmp *qmp;
	ktime_t ddr_freqsync_msg_time;
};

static struct stats_drvdata *drv;
static u64 deep_sleep_last_exited_time;

struct sleep_stats {
	u32 stat_type;
	u32 count;
	u64 last_entered_at;
	u64 last_exited_at;
	u64 accumulated;
};

struct appended_stats {
	u32 client_votes;
	u32 reserved[3];
};

struct island_stats {
	char name[MAX_ISLAND_STATS_NAME_LENGTH];
	u32 count;
	u64 last_entered_at;
	u64 last_exited_at;
	u64 accumulated;
	u32 vid;
	u32 task_id;
	u32 reserved[3];
};

static bool subsystem_stats_debug_on;
/* Subsystem stats before and after suspend */
static struct sleep_stats *b_subsystem_stats;
static struct sleep_stats *a_subsystem_stats;
static struct sleep_stats *c_subsystem_stats;
/* System sleep stats before and after suspend */
static struct sleep_stats *b_system_stats;
static struct sleep_stats *a_system_stats;
static DEFINE_MUTEX(sleep_stats_mutex);

static int subsystem_sleep_stats(struct sleep_stats *stats,
					unsigned int pid, unsigned int idx, unsigned int index)
{
	struct sleep_stats *subsystems_data;

	if (pid == SUBSYSTEM_STATS_OTHERS_NUM)
		memcpy_fromio(stats, drv->d[index].base, sizeof(*stats));
	else {
		subsystems_data = qcom_smem_get(pid, idx, NULL);
		if (IS_ERR(subsystems_data))
			return -ENODEV;

		stats->count = subsystems_data->count;
		stats->last_entered_at = subsystems_data->last_entered_at;
		stats->last_exited_at = subsystems_data->last_exited_at;
		stats->accumulated = subsystems_data->accumulated;
	}

	return 0;
}

static inline void get_sleep_stat_name(u32 type, char *stat_type)
{
	int i;

	for (i = 0; i < sizeof(u32); i++) {
		stat_type[i] = type & 0xff;
		type = type >> 8;
	}
	strim(stat_type);
}

bool has_system_slept(void)
{
	int i;
	bool sleep_flag = true;
	char stat_type[sizeof(u32) + 1] = {0};

	for (i = 0; i < drv->config->num_records; i++) {
		if (b_system_stats[i].count == a_system_stats[i].count) {
			get_sleep_stat_name(b_system_stats[i].stat_type, stat_type);
			pr_warn("System %s has not entered sleep\n", stat_type);
			sleep_flag = false;
		}
	}

	return sleep_flag;
}
EXPORT_SYMBOL_GPL(has_system_slept);

bool has_subsystem_slept(void)
{
	int i;
	bool sleep_flag = true;

	for (i = 0; i < ARRAY_SIZE(subsystems); i++) {
		if (subsystems[i].not_present)
			continue;

		if ((b_subsystem_stats[i].count == a_subsystem_stats[i].count) &&
			(a_subsystem_stats[i].last_exited_at >
				a_subsystem_stats[i].last_entered_at)) {
			pr_warn("Subsystem %s has not entered sleep\n", subsystems[i].name);
			sleep_flag = false;
		}
	}

	return sleep_flag;
}
EXPORT_SYMBOL_GPL(has_subsystem_slept);

bool current_subsystem_sleep(void)
{
	int i, ret;
	bool sleep_flag = true;

	for (i = 0; i < ARRAY_SIZE(subsystems); i++) {
		ret = subsystem_sleep_stats(c_subsystem_stats + i,
					subsystems[i].pid, subsystems[i].smem_item, i);
		if (ret != -ENODEV && subsystems[i].smem_item != APSS) {
			if (c_subsystem_stats[i].last_exited_at >
					c_subsystem_stats[i].last_entered_at) {
				pr_warn("Subsystem %s not in sleep\n", subsystems[i].name);
				sleep_flag = false;
				break;
			}
		}
	}
	return sleep_flag;
}
EXPORT_SYMBOL_GPL(current_subsystem_sleep);

void subsystem_sleep_debug_enable(bool enable)
{
	subsystem_stats_debug_on = enable;
}
EXPORT_SYMBOL_GPL(subsystem_sleep_debug_enable);

static inline int qcom_stats_copy_to_user(unsigned long arg, struct sleep_stats *stats,
					  unsigned long size)
{

	return copy_to_user((void __user *)arg, stats, size);
}

static inline void qcom_stats_update_accumulated_duration(struct sleep_stats *stats)
{
	/*
	 * If a subsystem is in sleep when reading the sleep stats from SMEM
	 * adjust the accumulated sleep duration to show actual sleep time.
	 * This ensures that the displayed stats are real when used for
	 * the purpose of computing battery utilization.
	 */
	if (stats->last_entered_at > stats->last_exited_at)
		stats->accumulated += (__arch_counter_get_cntvct() - stats->last_entered_at);
}

static inline void qcom_stats_copy(struct sleep_stats *src, struct sleep_stats *dst)
{
	dst->stat_type = src->stat_type;
	dst->count = src->count;
	dst->last_entered_at = src->last_entered_at;
	dst->last_exited_at = src->last_exited_at;
	dst->accumulated = src->accumulated;
}

static bool ddr_stats_is_freq_overtime(struct sleep_stats *data)
{
	if ((data->count == 0) && (drv->config->ddr_freq_update))
		return true;

	return false;
}

uint64_t get_aosd_sleep_exit_time(void)
{
	int i;
	u64 last_exited_at;
	u32 count;
	static u32 saved_deep_sleep_count;
	u32 s_type = 0;
	char stat_type[5] = {0};

	for (i = 0; i < drv->config->num_records; i++) {
		s_type = readl_relaxed(drv->d[i].base);
		memcpy(stat_type, &s_type, sizeof(u32));
		strim(stat_type);

		if (!memcmp((const void *)stat_type, (const void *)"aosd", 4)) {
			count = readl_relaxed(drv->d[i].base + COUNT_OFFSET);

			if (saved_deep_sleep_count == count)
				deep_sleep_last_exited_time = 0;
			else {
				saved_deep_sleep_count = count;
				last_exited_at = readq_relaxed(drv->d[i].base +
				LAST_EXITED_AT_OFFSET);
				deep_sleep_last_exited_time = last_exited_at;
			}
			break;

		}
	}

	return deep_sleep_last_exited_time;
}
EXPORT_SYMBOL_GPL(get_aosd_sleep_exit_time);

static u64 qcom_stats_fill_ddr_stats(void __iomem *reg, struct sleep_stats *data, u32 *entry_count)
{
	u64 accumulated_duration = 0;
	int i;

	*entry_count = readl_relaxed(reg + DDR_STATS_NUM_MODES_ADDR);
	if (*entry_count > DDR_STATS_MAX_NUM_MODES) {
		pr_err("Invalid entry count\n");
		return 0;
	}

	reg += DDR_STATS_ENTRY_ADDR;

	for (i = 0; i < *entry_count; i++) {
		data[i].count = readl_relaxed(reg + DDR_STATS_COUNT_ADDR);
		if ((i >= 0x4) && (ddr_stats_is_freq_overtime(&data[i]))) {
			pr_err("ddr_stats: Freq update failed\n");
			return 0;
		}

		data[i].stat_type = readl_relaxed(reg + DDR_STATS_NAME_ADDR);
		data[i].last_entered_at = 0xDEADDEAD;
		data[i].last_exited_at = 0xDEADDEAD;
		data[i].accumulated = readq_relaxed(reg + DDR_STATS_DURATION_ADDR);

		accumulated_duration += data[i].accumulated;
		reg += sizeof(struct sleep_stats) - 2 * sizeof(u64);
	}

	return accumulated_duration;
}

static int qcom_stats_device_open(struct inode *inode, struct file *file)
{
	struct stats_drvdata *drv = NULL;

	if (!inode || !inode->i_cdev || !file)
		return -EINVAL;

	drv = container_of(inode->i_cdev, struct stats_drvdata, stats_cdev);
	file->private_data = drv;

	return 0;
}

int qcom_stats_ddr_freqsync_msg(void)
{
	static const char buf[MAX_MSG_LEN] = "{class: ddr, action: freqsync}";
	int ret = 0;

	if (!drv || !drv->qmp || !drv->config->read_ddr_votes)
		return -ENODEV;

	mutex_lock(&drv->lock);
	ret = qmp_send(drv->qmp, buf, sizeof(buf));
	if (ret) {
		pr_err("Error sending qmp message: %d\n", ret);
		mutex_unlock(&drv->lock);
		return ret;
	}

	mutex_unlock(&drv->lock);

	drv->ddr_freqsync_msg_time = ktime_get_boottime();

	return ret;
}
EXPORT_SYMBOL(qcom_stats_ddr_freqsync_msg);

static int qcom_stats_ddr_freq_sync(int *modes, struct sleep_stats *stat)
{
	void __iomem *reg = NULL;
	u32 entry_count, name;
	ktime_t now;
	int i, j, ret;

	if (drv->config->read_ddr_votes) {
		ret = qcom_stats_ddr_freqsync_msg();
		if (ret)
			return ret;

		now = ktime_get_boottime();
		while (now < drv->ddr_freqsync_msg_time) {
			udelay(500);
			now = ktime_get_boottime();
		}
	}

	reg = drv->base + drv->config->ddr_stats_offset;
	qcom_stats_fill_ddr_stats(reg, stat, &entry_count);

	if (drv->config->read_ddr_votes) {
		for (i = 0, j = 0; i < entry_count; i++) {
			name = (stat[i].stat_type >> 8) & 0xFF;
			if (name == 0x1 && !stat[i].count)
				break;
			++j;
		}
		if (j < DDR_STATS_MAX_NUM_MODES)
			*modes = j;
	}

	return 0;
}

static long qcom_stats_device_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	struct stats_drvdata *drv = file->private_data;
	const struct subsystem_data *subsystem = NULL;
	struct sleep_stats *stat;
	struct sleep_stats *temp;
	void __iomem *reg = NULL;
	unsigned long size = sizeof(struct sleep_stats);
	u32 stats_id;
	int ret;

	mutex_lock(&sleep_stats_mutex);
	if (cmd != DDR_STATS_IOCTL)
		stat = kzalloc(sizeof(struct sleep_stats), GFP_KERNEL);
	else
		stat = kcalloc(DDR_STATS_MAX_NUM_MODES, sizeof(struct sleep_stats), GFP_KERNEL);
	if (!stat) {
		mutex_unlock(&sleep_stats_mutex);
		return -ENOMEM;
	}

	switch (cmd) {
	case MODEM_IOCTL:
		subsystem = &subsystems[0];
		break;
	case WPSS_IOCTL:
		subsystem = &subsystems[1];
		break;
	case ADSP_IOCTL:
		subsystem = &subsystems[2];
		break;
	case CDSP_IOCTL:
		subsystem = &subsystems[3];
		break;
	case SLPI_IOCTL:
		subsystem = &subsystems[4];
		break;
	case GPU_IOCTL:
		subsystem = &subsystems[5];
		break;
	case DISPLAY_IOCTL:
		subsystem = &subsystems[6];
		break;
	case ADSP_ISLAND_IOCTL:
		subsystem = &subsystems[7];
		break;
	case SLPI_ISLAND_IOCTL:
		subsystem = &subsystems[8];
		break;
	case APSS_IOCTL:
		subsystem = &subsystems[9];
		break;
	case AOSD_IOCTL:
		stats_id = 0;
		if (drv->config->num_records > stats_id)
			reg = drv->d[stats_id].base;
		break;
	case CXSD_IOCTL:
		stats_id = 1;
		if (drv->config->num_records > stats_id)
			reg = drv->d[stats_id].base;
		break;
	case DDR_IOCTL:
		stats_id = 2;
		if (drv->config->num_records > stats_id)
			reg = drv->d[stats_id].base;
		break;
	case DDR_STATS_IOCTL:
		break;
	default:
		pr_err("Incorrect command error\n");
		ret = -EINVAL;
		goto exit;
	}

	if (subsystem) {
		/* Items are allocated lazily, so lookup pointer each time */
		temp = qcom_smem_get(subsystem->pid, subsystem->smem_item, NULL);
		if (IS_ERR(temp)) {
			ret = -EIO;
			goto exit;
		}

		qcom_stats_copy(temp, stat);

		qcom_stats_update_accumulated_duration(stat);

		ret = qcom_stats_copy_to_user(arg, stat, size);
	} else if (reg) {
		memcpy_fromio(stat, reg, sizeof(*stat));

		qcom_stats_update_accumulated_duration(stat);

		ret = qcom_stats_copy_to_user(arg, stat, size);
	} else {
		int modes = DDR_STATS_MAX_NUM_MODES;

		ret = qcom_stats_ddr_freq_sync(&modes, stat);
		if (ret)
			goto exit;

		ret = qcom_stats_copy_to_user(arg, stat, modes * size);
	}

exit:
	kfree(stat);
	mutex_unlock(&sleep_stats_mutex);
	return ret;
}

static const struct file_operations qcom_stats_device_fops = {
	.owner		=	THIS_MODULE,
	.open		=	qcom_stats_device_open,
	.unlocked_ioctl =	qcom_stats_device_ioctl,
};

int ddr_stats_get_freq_count(void)
{
	u32 entry_count, name;
	u32 freq_count = 0;
	void __iomem *reg;
	int i;

	if (!drv || !drv->qmp || !drv->config->read_ddr_votes)
		return -ENODEV;

	reg = drv->base + drv->config->ddr_stats_offset;
	entry_count = readl_relaxed(reg + DDR_STATS_NUM_MODES_ADDR);
	if (entry_count > DDR_STATS_MAX_NUM_MODES) {
		pr_err("Invalid entry count\n");
		return 0;
	}

	reg += DDR_STATS_ENTRY_ADDR;

	for (i = 0; i < entry_count; i++) {
		name = readl_relaxed(reg + DDR_STATS_NAME_ADDR);
		name = (name >> 8) & 0xFF;
		if (name == 0x1)
			freq_count++;

		reg += sizeof(struct sleep_stats) - 2 * sizeof(u64);
	}

	return freq_count;
}
EXPORT_SYMBOL(ddr_stats_get_freq_count);

int ddr_stats_get_residency(int freq_count, struct ddr_freq_residency *data)
{
	struct sleep_stats stat[DDR_STATS_MAX_NUM_MODES];
	void __iomem *reg;
	u32 name, entry_count;
	ktime_t now;
	int i, j;

	if (freq_count < 0 || !data)
		return -EINVAL;

	if (!drv || !drv->qmp || !drv->config->read_ddr_votes)
		return -ENODEV;

	now = ktime_get_boottime();
	while (now < drv->ddr_freqsync_msg_time) {
		udelay(500);
		now = ktime_get_boottime();
	}

	mutex_lock(&drv->lock);

	reg = drv->base + drv->config->ddr_stats_offset;
	qcom_stats_fill_ddr_stats(reg, stat, &entry_count);

	for (i = 0, j = 0; i < entry_count; i++) {
		name = stat[i].stat_type;
		if (((name >> 8) & 0xFF) == 0x1 && stat[i].count) {
			data[j].freq = name >> 16;
			data[j].residency = stat[i].accumulated;
			if (++j > freq_count)
				break;
		}
	}

	mutex_unlock(&drv->lock);

	return j;
}
EXPORT_SYMBOL(ddr_stats_get_residency);

int ddr_stats_get_ss_count(void)
{
	return drv->config->read_ddr_votes ? MAX_DRV : -EOPNOTSUPP;
}
EXPORT_SYMBOL(ddr_stats_get_ss_count);

static void __iomem *qcom_stats_get_ddr_stats_data_addr(void)
{
	void __iomem *reg = NULL;
	u32 vote_offset;
	u32 entry_count;

	reg = drv->base + drv->config->ddr_stats_offset;
	entry_count = readl_relaxed(reg + DDR_STATS_NUM_MODES_ADDR);
	if (entry_count > DDR_STATS_MAX_NUM_MODES) {
		pr_err("Invalid entry count\n");
		return NULL;
	}

	vote_offset = DDR_STATS_ENTRY_ADDR;
	vote_offset +=  entry_count * (sizeof(struct sleep_stats) - 2 * sizeof(u64));
	reg = drv->base + drv->config->ddr_stats_offset + vote_offset;

	return reg;
}

int ddr_stats_get_ss_vote_info(int ss_count,
			       struct ddr_stats_ss_vote_info *vote_info)
{
	static const char buf[MAX_MSG_LEN] = "{class: ddr, res: drvs_ddr_votes}";
	u32 val[MAX_DRV];
	void __iomem *reg;
	int ret, i;

	if (!vote_info || !(ss_count == MAX_DRV) || !drv)
		return -ENODEV;

	if (!drv->qmp)
		return -EOPNOTSUPP;

	mutex_lock(&drv->lock);
	ret = qmp_send(drv->qmp, buf, sizeof(buf));
	if (ret) {
		pr_err("Error sending qmp message: %d\n", ret);
		mutex_unlock(&drv->lock);
		return ret;
	}

	reg = qcom_stats_get_ddr_stats_data_addr();
	if (!reg) {
		pr_err("Error getting ddr stats data addr\n");
		mutex_unlock(&drv->lock);
		return -EINVAL;
	}

	for (i = 0; i < ss_count; i++, reg += sizeof(u32)) {
		val[i] = readl_relaxed(reg);
		if (val[i] == DRV_ABSENT) {
			vote_info[i].ab = DRV_ABSENT;
			vote_info[i].ib = DRV_ABSENT;
			continue;
		} else if (val[i] == DRV_INVALID) {
			vote_info[i].ab = DRV_INVALID;
			vote_info[i].ib = DRV_INVALID;
			continue;
		}

		vote_info[i].ab = (val[i] >> VOTE_X_SHIFT) & VOTE_MASK;
		vote_info[i].ib = val[i] & VOTE_MASK;
	}

	mutex_unlock(&drv->lock);
	return 0;
}
EXPORT_SYMBOL(ddr_stats_get_ss_vote_info);

static void cxvt_info_fill_data(void __iomem *reg, u32 entry_count,
								u32 *data)
{
	int i;

	for (i = 0; i < entry_count; i++) {
		data[i] = readl_relaxed(reg);
		reg += sizeof(u32);
	}
}

int cx_stats_get_ss_vote_info(int ss_count,
			       struct qcom_stats_cx_vote_info *vote_info)
{
	static const char buf[MAX_MSG_LEN] = "{class: misc_debug, res: cx_vote}";
	void __iomem *reg;
	int ret;
	int i, j;
	u32 data[((MAX_DRV + 0x3) & (~0x3))/4];

	if (!vote_info || !(ss_count == MAX_DRV) || !drv)
		return -ENODEV;

	if (!drv->qmp || !drv->config->cx_vote_offset)
		return -EOPNOTSUPP;

	mutex_lock(&drv->lock);
	ret = qmp_send(drv->qmp, buf, sizeof(buf));
	if (ret) {
		pr_err("Error sending qmp message: %d\n", ret);
		mutex_unlock(&drv->lock);
		return ret;
	}

	reg = qcom_stats_get_ddr_stats_data_addr();
	if (!reg) {
		pr_err("Error getting ddr stats data addr\n");
		mutex_unlock(&drv->lock);
		return -EINVAL;
	}

	cxvt_info_fill_data(reg, ((MAX_DRV + 0x3) & (~0x3))/4, data);
	for (i = 0, j = 0; i < ((MAX_DRV + 0x3) & (~0x3))/4; i++, j += 4) {
		vote_info[j].level = (data[i] & 0xff);
		vote_info[j+1].level = ((data[i] & 0xff00) >> 8);
		vote_info[j+2].level = ((data[i] & 0xff0000) >> 16);
		vote_info[j+3].level = ((data[i] & 0xff000000) >> 24);
	}

	mutex_unlock(&drv->lock);
	return 0;
}
EXPORT_SYMBOL(cx_stats_get_ss_vote_info);

static void qcom_print_stats(struct seq_file *s, const struct sleep_stats *stat)
{
	u64 accumulated = stat->accumulated;
	/*
	 * If a subsystem is in sleep when reading the sleep stats adjust
	 * the accumulated sleep duration to show actual sleep time.
	 */
	if (stat->last_entered_at > stat->last_exited_at)
		accumulated += arch_timer_read_counter() - stat->last_entered_at;

	seq_printf(s, "Count: %u\n", stat->count);
	seq_printf(s, "Last Entered At: %llu\n", stat->last_entered_at);
	seq_printf(s, "Last Exited At: %llu\n", stat->last_exited_at);
	seq_printf(s, "Accumulated Duration: %llu\n", accumulated);
}

static int qcom_subsystem_sleep_stats_show(struct seq_file *s, void *unused)
{
	struct subsystem_data *subsystem = s->private;
	struct sleep_stats *stat;

	/* Items are allocated lazily, so lookup pointer each time */
	stat = qcom_smem_get(subsystem->pid, subsystem->smem_item, NULL);
	if (IS_ERR(stat))
		return 0;

	qcom_print_stats(s, stat);

	return 0;
}

static int qcom_soc_sleep_stats_show(struct seq_file *s, void *unused)
{
	struct stats_data *d = s->private;
	void __iomem *reg = d->base;
	struct sleep_stats stat;

	memcpy_fromio(&stat, reg, sizeof(stat));
	qcom_print_stats(s, &stat);

	if (d->appended_stats_avail) {
		struct appended_stats votes;

		memcpy_fromio(&votes, reg + CLIENT_VOTES_OFFSET, sizeof(votes));
		seq_printf(s, "Client Votes: %#x\n", votes.client_votes);
	}

	return 0;
}

static void print_ddr_stats(struct seq_file *s, int *count,
			     struct sleep_stats *data, u64 accumulated_duration)
{
	u32 cp_idx = 0;
	u32 name, duration = 0;

	if (accumulated_duration)
		duration = (data->accumulated * 100) / accumulated_duration;

	name = (data->stat_type >> 8) & 0xFF;
	if (name == 0x0) {
		name = (data->stat_type) & 0xFF;
		*count = *count + 1;
		seq_printf(s,
		"LPM %d:\tName:0x%x\tcount:%u\tDuration (ticks):%ld (~%d%%)\n",
			*count, name, data->count, data->accumulated, duration);
	} else if (name == 0x1) {
		cp_idx = data->stat_type & 0x1F;
		name = data->stat_type >> 16;

		if (!name || !data->count)
			return;

		seq_printf(s,
		"Freq %dMhz:\tCP IDX:%u\tcount:%u\tDuration (ticks):%ld (~%d%%)\n",
			name, cp_idx, data->count, data->accumulated, duration);
	}
}

static int ddr_stats_show(struct seq_file *s, void *d)
{
	struct sleep_stats data[DDR_STATS_MAX_NUM_MODES];
	void __iomem *reg = s->private;
	u32 entry_count;
	u64 accumulated_duration = 0, accumulated_duration_ddr_mode = 0;
	int i, lpm_count = 0;

	accumulated_duration = qcom_stats_fill_ddr_stats(reg, data, &entry_count);

	for (i = 0; i < DDR_STATS_NUM_MODES_ADDR; i++)
		accumulated_duration_ddr_mode += data[i].accumulated;

	for (i = 0; i < DDR_STATS_NUM_MODES_ADDR; i++)
		print_ddr_stats(s, &lpm_count, &data[i], accumulated_duration_ddr_mode);

	if (!accumulated_duration) {
		seq_puts(s, "ddr_stats: Freq update failed.\n");
		return 0;
	}

	accumulated_duration -= accumulated_duration_ddr_mode;
	for (i = DDR_STATS_NUM_MODES_ADDR; i < entry_count; i++)
		print_ddr_stats(s, &lpm_count, &data[i], accumulated_duration);

	return 0;
}

static int island_stats_show(struct seq_file *s, void *unused)
{
	struct island_stats *stat;
	int i;

	/* Items are allocated lazily, so lookup pointer each time */
	stat = qcom_smem_get(ISLAND_STATS_PID, ISLAND_STATS_SMEM_ID, NULL);
	if (IS_ERR(stat))
		return 0;

	for (i = 0; i < MAX_ISLAND_STATS; i++) {
		if (!strcmp(stat[i].name, "DEADDEAD"))
			continue;

		seq_printf(s, "Name: %s\n", stat[i].name);
		seq_printf(s, "Count: %u\n", stat[i].count);
		seq_printf(s, "Last Entered At: %llu\n", stat[i].last_entered_at);
		seq_printf(s, "Last Exited At: %llu\n", stat[i].last_exited_at);
		seq_printf(s, "Accumulated Duration: %llu\n", stat[i].accumulated);
		seq_printf(s, "Vid: %u\n", stat[i].vid);
		seq_printf(s, "task_id: %u\n", stat[i].task_id);
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(qcom_soc_sleep_stats);
DEFINE_SHOW_ATTRIBUTE(qcom_subsystem_sleep_stats);
DEFINE_SHOW_ATTRIBUTE(ddr_stats);
DEFINE_SHOW_ATTRIBUTE(island_stats);

static int qcom_create_stats_device(struct stats_drvdata *drv)
{
	int ret;

	ret = alloc_chrdev_region(&drv->dev_no, STATS_BASEMINOR, STATS_MAX_MINOR,
				  STATS_DEVICE_NAME);
	if (ret)
		return ret;

	cdev_init(&drv->stats_cdev, &qcom_stats_device_fops);
	ret = cdev_add(&drv->stats_cdev, drv->dev_no, 1);
	if (ret) {
		unregister_chrdev_region(drv->dev_no, 1);
		return ret;
	}

	drv->stats_class = class_create(THIS_MODULE, STATS_DEVICE_NAME);
	if (IS_ERR_OR_NULL(drv->stats_class)) {
		cdev_del(&drv->stats_cdev);
		unregister_chrdev_region(drv->dev_no, 1);
		return PTR_ERR(drv->stats_class);
	}

	drv->stats_device = device_create(drv->stats_class, NULL, drv->dev_no, NULL,
					  STATS_DEVICE_NAME);
	if (IS_ERR_OR_NULL(drv->stats_device)) {
		class_destroy(drv->stats_class);
		cdev_del(&drv->stats_cdev);
		unregister_chrdev_region(drv->dev_no, 1);
		return PTR_ERR(drv->stats_device);
	}

	return ret;
}

static void qcom_create_island_stat_files(struct dentry *root, void __iomem *reg,
					  struct stats_data *d,
					  const struct stats_config *config)
{
	if (!config->island_stats_avail)
		return;

	debugfs_create_file("island_stats", 0400, root, NULL, &island_stats_fops);
}

static void qcom_create_ddr_stat_files(struct dentry *root, void __iomem *reg,
					     struct stats_data *d,
					     const struct stats_config *config)
{
	size_t stats_offset;
	u32 key;

	if (!config->ddr_stats_offset)
		return;

	stats_offset = config->ddr_stats_offset;

	key = readl_relaxed(reg + stats_offset + DDR_STATS_MAGIC_KEY_ADDR);
	if (key == DDR_STATS_MAGIC_KEY)
		debugfs_create_file("ddr_stats", 0400, root, reg + stats_offset, &ddr_stats_fops);
}

static void qcom_create_soc_sleep_stat_files(struct dentry *root, void __iomem *reg,
					     struct stats_data *d,
					     const struct stats_config *config)
{
	char stat_type[sizeof(u32) + 1] = {0};
	size_t stats_offset = config->stats_offset;
	u32 offset = 0, type;
	int i;

	/*
	 * On RPM targets, stats offset location is dynamic and changes from target
	 * to target and sometimes from build to build for same target.
	 *
	 * In such cases the dynamic address is present at 0x14 offset from base
	 * address in devicetree. The last 16bits indicates the stats_offset.
	 */
	if (config->dynamic_offset) {
		stats_offset = readl(reg + RPM_DYNAMIC_ADDR);
		stats_offset &= RPM_DYNAMIC_ADDR_MASK;
	}

	for (i = 0; i < config->num_records; i++) {
		d[i].base = reg + offset + stats_offset;

		/*
		 * Read the low power mode name and create debugfs file for it.
		 * The names read could be of below,
		 * (may change depending on low power mode supported).
		 * For rpmh-sleep-stats: "aosd", "cxsd" and "ddr".
		 * For rpm-sleep-stats: "vmin" and "vlow".
		 */
		type = readl(d[i].base);
		get_sleep_stat_name(type, stat_type);
		debugfs_create_file(stat_type, 0400, root, &d[i],
				    &qcom_soc_sleep_stats_fops);

		offset += sizeof(struct sleep_stats);
		if (d[i].appended_stats_avail)
			offset += sizeof(struct appended_stats);
	}
}

static void qcom_create_subsystem_stat_files(struct dentry *root,
					     const struct stats_config *config,
					     struct device_node *node)
{
	int i, j, n_subsystems;
	const char *name;

	if (!config->subsystem_stats_in_smem)
		return;

	n_subsystems = of_property_count_strings(node, "ss-name");
	if (n_subsystems < 0)
		return;

	for (i = 0; i < n_subsystems; i++) {
		of_property_read_string_index(node, "ss-name", i, &name);

		for (j = 0; j < ARRAY_SIZE(subsystems); j++) {
			if (!strcmp(subsystems[j].name, name)) {
				debugfs_create_file(subsystems[j].name, 0400, root,
						    (void *)&subsystems[j],
						    &qcom_subsystem_sleep_stats_fops);
				break;
			}
		}
	}
}

static int qcom_stats_probe(struct platform_device *pdev)
{
	void __iomem *reg;
	struct dentry *root;
	const struct stats_config *config;
	struct stats_data *d;
	int i;
	int ret;

	config = device_get_match_data(&pdev->dev);
	if (!config)
		return -ENODEV;

	reg = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(reg))
		return -ENOMEM;

	d = devm_kcalloc(&pdev->dev, config->num_records,
			 sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	drv = devm_kzalloc(&pdev->dev, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	for (i = 0; i < config->num_records; i++)
		d[i].appended_stats_avail = config->appended_stats_avail;

	root = debugfs_create_dir("qcom_stats", NULL);

	qcom_create_subsystem_stat_files(root, config, pdev->dev.of_node);
	qcom_create_soc_sleep_stat_files(root, reg, d, config);
	qcom_create_ddr_stat_files(root, reg, d, config);
	qcom_create_island_stat_files(root, reg, d, config);

	drv->d = d;
	drv->config = config;
	drv->base = reg;
	drv->root = root;
	drv->ddr_freqsync_msg_time = 0;
	mutex_init(&drv->lock);

	ret = qcom_create_stats_device(drv);
	if (ret)
		goto fail_create_stats_device;

	if (config->read_ddr_votes && config->ddr_stats_offset) {
		drv->qmp = qmp_get(&pdev->dev);
		if (IS_ERR(drv->qmp)) {
			ret = PTR_ERR(drv->qmp);
			goto fail;
		}
	}

	subsystem_stats_debug_on = false;
	b_subsystem_stats = devm_kcalloc(&pdev->dev, ARRAY_SIZE(subsystems),
					sizeof(struct sleep_stats), GFP_KERNEL);
	if (!b_subsystem_stats) {
		ret = -ENOMEM;
		goto fail;
	}

	a_subsystem_stats = devm_kcalloc(&pdev->dev, ARRAY_SIZE(subsystems),
					sizeof(struct sleep_stats), GFP_KERNEL);
	if (!a_subsystem_stats) {
		ret = -ENOMEM;
		goto fail;
	}

	c_subsystem_stats = devm_kcalloc(&pdev->dev, ARRAY_SIZE(subsystems),
					 sizeof(struct sleep_stats), GFP_KERNEL);
	if (!c_subsystem_stats) {
		ret = -ENOMEM;
		goto fail;
	}

	b_system_stats = devm_kcalloc(&pdev->dev, drv->config->num_records,
					sizeof(struct sleep_stats), GFP_KERNEL);
	if (!b_system_stats) {
		ret = -ENOMEM;
		goto fail;
	}

	a_system_stats = devm_kcalloc(&pdev->dev, drv->config->num_records,
					sizeof(struct sleep_stats), GFP_KERNEL);
	if (!a_system_stats) {
		ret = -ENOMEM;
		goto fail;
	}

	platform_set_drvdata(pdev, drv);

	return 0;

fail:
	device_destroy(drv->stats_class, drv->dev_no);
	class_destroy(drv->stats_class);
	cdev_del(&drv->stats_cdev);
	unregister_chrdev_region(drv->dev_no, 1);
fail_create_stats_device:
	debugfs_remove_recursive(drv->root);
	return ret;
}

static int qcom_stats_remove(struct platform_device *pdev)
{
	struct stats_drvdata *drv = platform_get_drvdata(pdev);

	device_destroy(drv->stats_class, drv->dev_no);
	class_destroy(drv->stats_class);
	cdev_del(&drv->stats_cdev);
	unregister_chrdev_region(drv->dev_no, 1);

	debugfs_remove_recursive(drv->root);

	return 0;
}

static int qcom_stats_suspend(struct device *dev)
{
	struct stats_drvdata *drv = dev_get_drvdata(dev);
	struct sleep_stats *tmp;
	void __iomem *reg = NULL;
	int i;
	u32 stats_id = 0;

	if (!subsystem_stats_debug_on)
		return 0;

	mutex_lock(&sleep_stats_mutex);
	for (i = 0; i < ARRAY_SIZE(subsystems); i++) {
		tmp = qcom_smem_get(subsystems[i].pid, subsystems[i].smem_item, NULL);
		if (IS_ERR(tmp)) {
			subsystems[i].not_present = true;
			continue;
		} else
			subsystems[i].not_present = false;
		qcom_stats_copy(tmp, b_subsystem_stats + i);
	}

	for (i = 0; i < drv->config->num_records; i++, stats_id++) {
		if (drv->config->num_records > stats_id)
			reg = drv->d[stats_id].base;
		if (reg)
			memcpy_fromio(b_system_stats + i, reg, sizeof(struct sleep_stats));
	}
	mutex_unlock(&sleep_stats_mutex);

	return 0;
}

static int qcom_stats_resume(struct device *dev)
{
	struct stats_drvdata *drv = dev_get_drvdata(dev);
	struct sleep_stats *tmp;
	void __iomem *reg = NULL;
	int i;
	u32 stats_id = 0;

	if (!subsystem_stats_debug_on)
		return 0;

	mutex_lock(&sleep_stats_mutex);
	for (i = 0; i < ARRAY_SIZE(subsystems); i++) {
		if (subsystems[i].not_present)
			continue;
		tmp = qcom_smem_get(subsystems[i].pid, subsystems[i].smem_item, NULL);
		if (IS_ERR(tmp))
			continue;
		qcom_stats_copy(tmp, a_subsystem_stats + i);
	}

	for (i = 0; i < drv->config->num_records; i++, stats_id++) {
		if (drv->config->num_records > stats_id)
			reg = drv->d[stats_id].base;
		if (reg)
			memcpy_fromio(a_system_stats + i, reg, sizeof(struct sleep_stats));
	}
	mutex_unlock(&sleep_stats_mutex);

	return 0;
}

static const struct stats_config rpm_data = {
	.stats_offset = 0,
	.num_records = 2,
	.appended_stats_avail = true,
	.dynamic_offset = true,
	.subsystem_stats_in_smem = false,
};

/* Older RPM firmwares have the stats at a fixed offset instead */
static const struct stats_config rpm_data_dba0 = {
	.stats_offset = 0xdba0,
	.num_records = 2,
	.appended_stats_avail = true,
	.dynamic_offset = false,
	.subsystem_stats_in_smem = false,
};

static const struct stats_config rpmh_data_sdm845 = {
	.stats_offset = 0x48,
	.num_records = 2,
	.appended_stats_avail = false,
	.dynamic_offset = false,
	.subsystem_stats_in_smem = true,
};

static const struct stats_config rpmh_data = {
	.stats_offset = 0x48,
	.ddr_stats_offset = 0xb8,
	.num_records = 3,
	.appended_stats_avail = false,
	.dynamic_offset = false,
	.subsystem_stats_in_smem = true,
};

static const struct stats_config rpmh_v2_data = {
	.stats_offset = 0x48,
	.ddr_stats_offset = 0xb8,
	.cx_vote_offset = 0xb8,
	.num_records = 3,
	.appended_stats_avail = false,
	.dynamic_offset = false,
	.subsystem_stats_in_smem = true,
	.read_ddr_votes = true,
};

static const struct stats_config rpmh_v3_data = {
	.stats_offset = 0x48,
	.ddr_stats_offset = 0xb8,
	.cx_vote_offset = 0xb8,
	.num_records = 3,
	.appended_stats_avail = false,
	.dynamic_offset = false,
	.subsystem_stats_in_smem = true,
	.read_ddr_votes = true,
	.ddr_freq_update = true,
};

static const struct stats_config rpmh_v4_data = {
	.stats_offset = 0x48,
	.ddr_stats_offset = 0xb8,
	.cx_vote_offset = 0xb8,
	.num_records = 3,
	.appended_stats_avail = false,
	.dynamic_offset = false,
	.subsystem_stats_in_smem = true,
	.read_ddr_votes = true,
	.ddr_freq_update = true,
	.island_stats_avail = true,
};

static const struct of_device_id qcom_stats_table[] = {
	{ .compatible = "qcom,apq8084-rpm-stats", .data = &rpm_data_dba0 },
	{ .compatible = "qcom,msm8226-rpm-stats", .data = &rpm_data_dba0 },
	{ .compatible = "qcom,msm8916-rpm-stats", .data = &rpm_data_dba0 },
	{ .compatible = "qcom,msm8974-rpm-stats", .data = &rpm_data_dba0 },
	{ .compatible = "qcom,rpm-stats", .data = &rpm_data },
	{ .compatible = "qcom,rpmh-stats", .data = &rpmh_data },
	{ .compatible = "qcom,rpmh-stats-v2", .data = &rpmh_v2_data },
	{ .compatible = "qcom,rpmh-stats-v3", .data = &rpmh_v3_data },
	{ .compatible = "qcom,rpmh-stats-v4", .data = &rpmh_v4_data },
	{ .compatible = "qcom,sdm845-rpmh-stats", .data = &rpmh_data_sdm845 },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_stats_table);

static const struct dev_pm_ops qcom_stats_pm_ops = {
	.suspend_late = qcom_stats_suspend,
	.resume_early = qcom_stats_resume,
};

static struct platform_driver qcom_stats = {
	.probe = qcom_stats_probe,
	.remove = qcom_stats_remove,
	.driver = {
		.name = "qcom_stats",
		.of_match_table = qcom_stats_table,
		.pm = &qcom_stats_pm_ops,
	},
};

static int __init qcom_stats_init(void)
{
	return platform_driver_register(&qcom_stats);
}
late_initcall(qcom_stats_init);

static void __exit qcom_stats_exit(void)
{
	platform_driver_unregister(&qcom_stats);
}
module_exit(qcom_stats_exit)

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. (QTI) Stats driver");
MODULE_LICENSE("GPL v2");

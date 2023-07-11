// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

/*
 * This driver exposes API for aDSP service layers to intimate session
 * start and stop. Based on the aDSP sessions and activity information
 * derived from SMEM statistics, the driver detects and acts on
 * possible aDSP sleep (voting related) issues.
 */

#define pr_fmt(fmt) "adsp_sleepmon: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/string.h>
#include <linux/err.h>
#include <linux/limits.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/time.h>
#include <linux/spinlock.h>
#include <linux/rpmsg.h>
#include <linux/debugfs.h>
#include <linux/soc/qcom/smem.h>
#include <linux/soc/qcom/adsp_sleepmon_stats.h>
#include <asm/arch_timer.h>
#include <linux/jiffies.h>
#include <linux/suspend.h>
#include <../../remoteproc/qcom_common.h>
#include <uapi/misc/adsp_sleepmon.h>

#define ADSPSLEEPMON_SMEM_ADSP_PID                              2
#define ADSPSLEEPMON_SLEEPSTATS_ADSP_SMEM_ID                    606
#define ADSPSLEEPMON_SLEEPSTATS_ADSP_LPI_SMEM_ID                613
#define ADSPSLEEPMON_DSPPMSTATS_SMEM_ID                         624
#define ADSPSLEEPMON_DSPPMSTATS_NUMPD                           5
#define ADSPSLEEPMON_DSPPMSTATS_PID_FILTER                      0x7F
#define ADSPSLEEPMON_DSPPMSTATS_AUDIO_PID                       2
#define ADSPSLEEPMON_SYSMONSTATS_SMEM_ID                        634
#define ADSPSLEEPMON_SYSMONSTATS_EVENTS_FEATURE_ID              2
#define ADSPSLEEPMON_SYS_CLK_TICKS_PER_SEC                      19200000
#define ADSPSLEEPMON_SYS_CLK_TICKS_PER_MILLISEC                 19200
#define ADSPSLEEPMON_LPI_WAIT_TIME                              15
#define ADSPSLEEPMON_LPM_WAIT_TIME                              5
#define ADSPSLEEPMON_LPM_WAIT_TIME_OVERALL                      60
#define ADSPSLEEPMON_MIN_REQUIRED_RESUMES                       5
#define QDSPPM_CLIENT_NAME_SIZE_SLEEPMON                        24
#define QDSPPM_NUM_OF_CLIENTS_SLEEPMON                          16
#define SLEEPMON_ADSP_FEATURE_INFO                              1
#define SLEEPMON_DSPPM_FEATURE_INFO                             2
#define SLEEPMON_LPI_ISSUE_FEATURE_INFO                         3
#define SLEEPMON_SEND_SSR_COMMAND                               4
#define SLEEPMON_RECEIVE_PANIC_COMMAND                          5
#define SLEEPMON_RECEIVE_USLEEP_CLIENTS                         6
#define SLEEPMON_ADSP_GLINK_VERSION                             0x2
#define SLEEPMON_PROCESS_NAME_SIZE                              20
#define ADSPSLEEPMON_AUDIO_CLIENT                               1
#define ADSPSLEEPMON_DEVICE_NAME_LOCAL                          "msm_adsp_sleepmon"
#define WAIT_FOR_DSPPM_CLIENTDATA_TIMEOUT                        msecs_to_jiffies(100)

struct sleep_stats {
	u32 stat_type;
	u32 count;
	u64 last_entered_at;
	u64 last_exited_at;
	u64 accumulated;
};

struct pd_clients {
	int pid;
	u32 num_active;
};

struct dsppm_stats {
	u32 version;
	u32 latency_us;
	u32 timestamp;
	struct pd_clients pd[ADSPSLEEPMON_DSPPMSTATS_NUMPD];
};

struct sysmon_event_stats {
	u32 core_clk;
	u32 ab_vote_lsb;
	u32 ab_vote_msb;
	u32 ib_vote_lsb;
	u32 ib_vote_msb;
	u32 sleep_latency;
	u32 timestamp_lsb;
	u32 timestamp_msb;
};

struct adspsleepmon_file {
	struct hlist_node hn;
	spinlock_t hlock;
	u32 b_connected;
	u32 num_sessions;
	u32 num_lpi_sessions;
};

struct adspsleepmon_audio {
	u32 num_sessions;
	u32 num_lpi_sessions;
};

struct sleepmon_mmpmmipsreqtype {
	u32 total;
	u32 per_thread;
};

struct sleepmon_mmpmbusbwdatausagetype {
	u64 bytes_persec;
	u32 usage_percentage;
};

struct sleepmon_mmpmmppsreqtype {
	u32 total;
	u32 floor_clock;
};

enum islandparticipationoptionstype {
	MMPM_ISLAND_PARTICIPATION_NONE            = 0x0,
	MMPM_ISLAND_PARTICIPATION_LPI             = 0x1,
	MMPM_ISLAND_PARTICIPATION_DISALLOW_LPI    = 0x2,
	MMPM_ISLAND_PARTICIPATION_UIMAGE          = 0x4,
	MMPM_ISLAND_PARTICIPATION_DISALLOW_UIMAGE = 0x8,
};

enum dsppm_client_data_fetch_status {
	DSPPM_CLIENT_DATA_FETCH_SUCCESS = 0x1,
	DSPPM_CLIENT_DATA_FAILED_ONGOING_COHERENT = 0x2,
	DSPPM_CLIENT_DATA_FAILED_DSPPM_RETURNED_NULL = 0x3,
};

struct islandparticipationtype {
	u32 islandopt; //islandparticipationoptionstype
};

struct sleepmon_qdsppmrequest {
	char clientname[QDSPPM_CLIENT_NAME_SIZE_SLEEPMON];
	u32 clientid;
	u64 timestamp;
	u8 b_valid;
	u32 poweron;
	struct sleepmon_mmpmmipsreqtype mips;
	struct sleepmon_mmpmmppsreqtype mpps;
	struct sleepmon_mmpmbusbwdatausagetype bw;
	u32 sleeplatency;
	char process[SLEEPMON_PROCESS_NAME_SIZE];
	struct islandparticipationtype islandparticipation;
	u32 reserved_1;
	u32 reserved_2;
	u32 reserved_3;
	u32 reserved_4;
	u32 reserved_5;
	u32 reserved_6;
};

struct sleepmon_mmpmaggregatedmipsdata {
	u32 mips;
	u32 mpps;
	u32 clock;
	u32 kfactor;
};

struct sleepmon_mmpmaggregatedbwtype {
	u64 adsp_ddr_ab;
	u64 adsp_ddr_ib;
	u64 snoc_ddr_ab;
	u64 snoc_ddr_ib;
	u32 ddr_latency;
	u32 snoc_latency;
};

struct sleepmon_mmpmaggregatedahbtype {
	u32 ahbe_hz;
	u32 ahbi_hz;
};

struct sleepmon_qdsppm_aggdata {
	struct sleepmon_mmpmaggregatedmipsdata agg_mips;
	struct sleepmon_mmpmaggregatedbwtype agg_bw;
	struct sleepmon_mmpmaggregatedahbtype agg_ahb;
	u32 agg_latency;
	u32 reserved_3;
	u32 reserved_4;
	u32 reserved_5;
	u32 reserved_6;
	u32 reserved_7;
	u32 reserved_8;
};

struct sleepmon_qdsppm_clients {
	u8 update_flag;
	u64 timestamp;
	struct sleepmon_qdsppmrequest clients[QDSPPM_NUM_OF_CLIENTS_SLEEPMON];
	struct sleepmon_qdsppm_aggdata agg_data;
	u8 result;
	u32 num_clients;
	u32 sysmon_qdsppm_version;
	u32 reserved_1;
	u32 reserved_2;
	u32 reserved_3;
	u32 reserved_4;
	u32 reserved_5;
	u32 reserved_6;
	u32 reserved_7;
	u32 reserved_8;
};

struct sleepmon_usleep_npa_clients {
	char clientname[67];
	u32 request;
};

struct sleepmon_rx_msg_t {
	u32 size;
	u32 ver_info;
	u32 feature_id;
	union {
		struct sleepmon_qdsppm_clients dsppm_clients;
		int lpi_panic;
		struct sleepmon_usleep_npa_clients sleepmon_usleep_npa;
	} featureStruct;
};

struct sleepmon_lpi_issue {
	u8 lpi_issue_detect;
	u8 panic_enable;
	u8 ssr_enable;
};

struct sleepmon_tx_msg_t {
	u32 size;
	u32 adsp_ver_info;
	u32 feature_id;
	union {
		u32 dsppm_client_signal;
		struct sleepmon_lpi_issue sleepmon_lpi_detect;
		u32 send_ssr_command;
	} fs;
};

struct sleepmon_request {
	struct list_head node;
	struct sleepmon_rx_msg_t msg;
	bool busy;
};

struct adspsleepmon {
	bool b_enable;
	bool timer_event;
	bool timer_pending;
	bool suspend_event;
	bool smem_init_done;
	bool suspend_init_done;
	bool b_panic_lpm;
	bool b_panic_lpi;
	bool b_config_panic_lpm;
	bool b_config_panic_lpi;
	bool b_config_adsp_panic_lpm;
	bool b_config_adsp_panic_lpi;
	bool b_config_adsp_panic_lpm_overall;
	bool b_config_adsp_ssr_enable;
	bool b_rpmsg_register;
	u32 lpm_wait_time;
	u32 lpi_wait_time;
	u32 lpm_wait_time_overall;
	u32 min_required_resumes;
	u32 accumulated_resumes;
	u64 accumulated_duration;
	struct completion sem;
	u32 adsp_version;
	struct rpmsg_device *rpmsgdev;
	struct mutex lock;
	struct cdev cdev;
	struct class *class;
	dev_t devno;
	struct device *dev;
	struct task_struct *worker_task;
	struct adspsleepmon_audio audio_stats;
	struct hlist_head audio_clients;
	struct sleep_stats backup_lpm_stats;
	unsigned long long backup_lpm_timestamp;
	struct sleep_stats backup_lpi_stats;
	unsigned long long backup_lpi_timestamp;
	struct sleep_stats *lpm_stats;
	struct sleep_stats *lpi_stats;
	struct dsppm_stats *dsppm_stats;
	struct sysmon_event_stats *sysmon_event_stats;
	struct sleepmon_qdsppm_clients dsppm_clients;
	struct dentry *debugfs_dir;
	struct dentry *debugfs_panic_file;
	struct dentry *debugfs_master_stats;
	struct dentry *debugfs_read_panic_state;
	struct dentry *debugfs_adsp_panic_file;
	struct dentry *debugfs_read_adsp_panic_state;
	phandle adsp_rproc_phandle;
	struct rproc *adsp_rproc;
};

static struct adspsleepmon g_adspsleepmon;
static void adspsleepmon_timer_cb(struct timer_list *unused);
static DEFINE_TIMER(adspsleep_timer, adspsleepmon_timer_cb);
static DECLARE_WAIT_QUEUE_HEAD(adspsleepmon_wq);

static int sleepmon_get_dsppm_client_stats(void)
{
	int result = -EINVAL;
	struct sleepmon_tx_msg_t rpmsg;

	if (g_adspsleepmon.rpmsgdev && g_adspsleepmon.adsp_version > 0) {
		rpmsg.adsp_ver_info = SLEEPMON_ADSP_GLINK_VERSION;
		rpmsg.feature_id = SLEEPMON_DSPPM_FEATURE_INFO;
		rpmsg.fs.dsppm_client_signal = true;
		rpmsg.size = sizeof(rpmsg);
		result = rpmsg_send(g_adspsleepmon.rpmsgdev->ept,
				&rpmsg,
				sizeof(rpmsg));
		if (result)
			pr_err("DSPPM client signal send failed :%u\n", result);
	}

	return result;

}

static int sleepmon_send_ssr_command(void)
{
	int result = -EINVAL;
	struct sleepmon_tx_msg_t rpmsg;

	if (g_adspsleepmon.rpmsgdev && g_adspsleepmon.adsp_version > 1) {

		if (g_adspsleepmon.b_config_adsp_ssr_enable) {
			if (g_adspsleepmon.adsp_rproc) {
				pr_info("Setting recovery flag for ADSP SSR\n");
				qcom_rproc_update_recovery_status(g_adspsleepmon.adsp_rproc, true);
			} else {
				pr_info("Couldn't find rproc handle for ADSP\n");
			}
		}

		rpmsg.adsp_ver_info = SLEEPMON_ADSP_GLINK_VERSION;
		rpmsg.feature_id = SLEEPMON_SEND_SSR_COMMAND;
		rpmsg.fs.send_ssr_command = 1;
		rpmsg.size = sizeof(rpmsg);
		result = rpmsg_send(g_adspsleepmon.rpmsgdev->ept,
				&rpmsg,
				sizeof(rpmsg));

		if (result) {
			pr_err("Send SSR command failed\n");
			if (g_adspsleepmon.b_config_adsp_ssr_enable) {
				if (g_adspsleepmon.adsp_rproc) {
					pr_info("Resetting recovery flag for ADSP SSR\n");
					qcom_rproc_update_recovery_status(
						g_adspsleepmon.adsp_rproc, false);
				}
			}

		}
	} else {
		pr_err("ADSP version doesn't support panic\n");
	}

	return result;
}

static int sleepmon_send_lpi_issue_command(void)
{
	int result = -EINVAL;
	struct sleepmon_tx_msg_t rpmsg;

	if (g_adspsleepmon.rpmsgdev && g_adspsleepmon.adsp_version > 1) {
		rpmsg.adsp_ver_info = SLEEPMON_ADSP_GLINK_VERSION;
		rpmsg.feature_id = SLEEPMON_LPI_ISSUE_FEATURE_INFO;
		rpmsg.fs.sleepmon_lpi_detect.lpi_issue_detect = 1;

		if (g_adspsleepmon.b_panic_lpi ||
				g_adspsleepmon.b_config_adsp_panic_lpi)
			rpmsg.fs.sleepmon_lpi_detect.panic_enable = 1;
		else
			rpmsg.fs.sleepmon_lpi_detect.panic_enable = 0;

		rpmsg.fs.sleepmon_lpi_detect.ssr_enable = 0;
		rpmsg.size = sizeof(rpmsg);
		result = rpmsg_send(g_adspsleepmon.rpmsgdev->ept,
				&rpmsg,
				sizeof(rpmsg));

		if (result)
			pr_err("Send LPI issue command failed\n");
	} else {
		pr_err("Send LPI issue command failed, ADSP version unsupported\n");
	}

	return result;
}

static int adspsleepmon_suspend_notify(struct notifier_block *nb,
							unsigned long mode, void *_unused)
{
	switch (mode) {
	case PM_POST_SUSPEND:
	{
		/*
		 * Resume notification (previously in suspend)
		 * TODO
		 * Not acquiring mutex here, see if it is needed!
		 */
		pr_info("PM_POST_SUSPEND\n");
		if (!g_adspsleepmon.audio_stats.num_sessions ||
			(g_adspsleepmon.audio_stats.num_sessions ==
				g_adspsleepmon.audio_stats.num_lpi_sessions)) {
			g_adspsleepmon.suspend_event = true;
			wake_up_interruptible(&adspsleepmon_wq);
		}
		break;
	}
	default:
		/*
		 * Not handling other PM states, just return
		 */
		break;
	}

	return 0;
}

static struct notifier_block adsp_sleepmon_pm_nb = {
	.notifier_call = adspsleepmon_suspend_notify,
};

static void update_sysmon_event_stats_ptr(void *stats, size_t size)
{
	u32 feature_size, feature_id, version;

	g_adspsleepmon.sysmon_event_stats = NULL;
	feature_id = *(u32 *)stats;
	version = feature_id & 0xFFFF;
	feature_size = (feature_id >> 16) & 0xFFF;
	feature_id = feature_id >> 28;

	while (size >= feature_size) {
		switch (feature_id) {
		case ADSPSLEEPMON_SYSMONSTATS_EVENTS_FEATURE_ID:
			g_adspsleepmon.sysmon_event_stats =
					(struct sysmon_event_stats *)(stats + sizeof(u32));
			size = 0;
		break;

		default:
			/*
			 * Unrecognized, feature, jump through to the next
			 */
			stats = stats + feature_size;
			if (size >= feature_size)
				size = size - feature_size;
			feature_id = *(u32 *)stats;
			feature_size = (feature_id >> 16) & 0xFFF;
			feature_id = feature_id >> 28;
			version = feature_id & 0xFFFF;
		break;
		}
	}
}

static int adspsleepmon_smem_init(void)
{
	size_t size;
	void *stats = NULL;

	g_adspsleepmon.lpm_stats = qcom_smem_get(
						ADSPSLEEPMON_SMEM_ADSP_PID,
						ADSPSLEEPMON_SLEEPSTATS_ADSP_SMEM_ID,
						&size);

	if (IS_ERR_OR_NULL(g_adspsleepmon.lpm_stats) ||
		(sizeof(struct sleep_stats) > size)) {
		pr_err("Failed to get sleep stats from SMEM for ADSP: %d, size: %d\n",
				PTR_ERR(g_adspsleepmon.lpm_stats), size);
		return -ENOMEM;
	}

	g_adspsleepmon.lpi_stats = qcom_smem_get(
						ADSPSLEEPMON_SMEM_ADSP_PID,
						ADSPSLEEPMON_SLEEPSTATS_ADSP_LPI_SMEM_ID,
						&size);

	if (IS_ERR_OR_NULL(g_adspsleepmon.lpi_stats) ||
		(sizeof(struct sleep_stats) > size)) {
		pr_err("Failed to get LPI sleep stats from SMEM for ADSP: %d, size: %d\n",
				PTR_ERR(g_adspsleepmon.lpi_stats), size);
		return -ENOMEM;
	}

	g_adspsleepmon.dsppm_stats = qcom_smem_get(
						   ADSPSLEEPMON_SMEM_ADSP_PID,
						ADSPSLEEPMON_DSPPMSTATS_SMEM_ID,
						&size);

	if (IS_ERR_OR_NULL(g_adspsleepmon.dsppm_stats) ||
		(sizeof(struct dsppm_stats) > size)) {
		pr_err("Failed to get DSPPM stats from SMEM for ADSP: %d, size: %d\n",
				PTR_ERR(g_adspsleepmon.dsppm_stats), size);
		return -ENOMEM;
	}

	stats = qcom_smem_get(ADSPSLEEPMON_SMEM_ADSP_PID,
						ADSPSLEEPMON_SYSMONSTATS_SMEM_ID,
						&size);

	if (IS_ERR_OR_NULL(stats) || !size) {
		pr_err("Failed to get SysMon stats from SMEM for ADSP: %d, size: %d\n",
				PTR_ERR(stats), size);
		return -ENOMEM;
	}

	update_sysmon_event_stats_ptr(stats, size);

	if (IS_ERR_OR_NULL(g_adspsleepmon.sysmon_event_stats)) {
		pr_err("Failed to get SysMon event stats from SMEM for ADSP\n");
		return -ENOMEM;
	}

	/*
	 * Register for Resume notifications
	 */
	if (!g_adspsleepmon.suspend_init_done) {
		register_pm_notifier(&adsp_sleepmon_pm_nb);
		g_adspsleepmon.suspend_init_done = true;
	}

	g_adspsleepmon.smem_init_done = true;

	return 0;
}

static int sleepmon_rpmsg_callback(struct rpmsg_device *dev, void *data,
		int len, void *priv, u32 addr)
{
	struct sleepmon_rx_msg_t *msg = (struct sleepmon_rx_msg_t *)data;

	if (!data || (len < sizeof(*msg))) {
		dev_err(&dev->dev,
		"Invalid message in rpmsg callback, length: %d, expected: %lu\n",
				len, sizeof(*msg));
		return -EINVAL;
	}

	if (msg->feature_id == SLEEPMON_ADSP_FEATURE_INFO) {
		g_adspsleepmon.adsp_version = msg->ver_info;
		pr_info("Received ADSP version 0x%x\n",
			g_adspsleepmon.adsp_version);

		/*
		 * ADSP is booting up, time to initialize
		 * number of sessions params and delete
		 * any pending timer. Also backup LPM
		 * stats.
		 */

		if (!g_adspsleepmon.smem_init_done)
			return 0;

		g_adspsleepmon.audio_stats.num_sessions = 0;
		g_adspsleepmon.audio_stats.num_lpi_sessions = 0;

		del_timer(&adspsleep_timer);
		g_adspsleepmon.timer_pending = false;
		g_adspsleepmon.accumulated_duration = 0;
		g_adspsleepmon.accumulated_resumes = 0;

		memcpy(&g_adspsleepmon.backup_lpm_stats,
			g_adspsleepmon.lpm_stats,
			sizeof(struct sleep_stats));

		g_adspsleepmon.backup_lpm_timestamp = __arch_counter_get_cntvct();
	}

	if (msg->feature_id == SLEEPMON_DSPPM_FEATURE_INFO) {
		g_adspsleepmon.dsppm_clients.result = 0;
		g_adspsleepmon.dsppm_clients = msg->featureStruct.dsppm_clients;
		complete(&g_adspsleepmon.sem);
		pr_debug("Received DSPPM data version: 0x%x\n",
			msg->ver_info);
	}

	if (msg->feature_id == SLEEPMON_RECEIVE_PANIC_COMMAND) {
		if (g_adspsleepmon.b_panic_lpi) {
			pr_err("ADSP LPI issue detected: Triggering panic\n");
			panic("ADSP_SLEEPMON: ADSP LPI issue detected");
		} else if (g_adspsleepmon.b_config_adsp_panic_lpi) {
			sleepmon_send_ssr_command();
		}
	}

	if (msg->feature_id == SLEEPMON_RECEIVE_USLEEP_CLIENTS) {
		pr_info("USleep_NPA_Client :%s, Client_request :%x\n",
			msg->featureStruct.sleepmon_usleep_npa.clientname,
				msg->featureStruct.sleepmon_usleep_npa.request);
	}

	return 0;
}

static int debugfs_panic_state_read(void *data, u64 *val)
{
	*val = g_adspsleepmon.b_panic_lpm | (g_adspsleepmon.b_panic_lpi << 1);

	return 0;
}

static int debugfs_panic_state_write(void *data, u64 val)
{
	if (!(val & 0x1))
		g_adspsleepmon.b_panic_lpm = false;
	else
		g_adspsleepmon.b_panic_lpm =
				g_adspsleepmon.b_config_panic_lpm;
	if (!(val & 0x2))
		g_adspsleepmon.b_panic_lpi = false;
	else
		g_adspsleepmon.b_panic_lpi =
					g_adspsleepmon.b_config_panic_lpi;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(panic_state_fops,
			debugfs_panic_state_read,
			debugfs_panic_state_write,
			"%u\n");


static int read_panic_state_show(struct seq_file *s, void *d)
{
	int val = g_adspsleepmon.b_panic_lpm | (g_adspsleepmon.b_panic_lpi << 1);

	if (val == 0)
		seq_puts(s, "\nPanic State: LPM and LPI panics Disabled\n");
	if (val == 1)
		seq_puts(s, "\nPanic State: LPM Panic enabled\n");
	if (val == 2)
		seq_puts(s, "\nPanic State: LPI Panic enabled\n");
	if (val == 3)
		seq_puts(s, "\nPanic State: LPI and LPM Panics enabled\n");

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(read_panic_state);

static int debugfs_adsp_panic_state_read(void *data, u64 *val)
{
	*val = g_adspsleepmon.b_config_adsp_panic_lpm |
		(g_adspsleepmon.b_config_adsp_panic_lpi << 1) |
		(g_adspsleepmon.b_config_adsp_panic_lpm_overall << 2) |
		(g_adspsleepmon.b_config_adsp_ssr_enable << 3);

	return 0;
}

static int debugfs_adsp_panic_state_write(void *data, u64 val)
{
	if (!g_adspsleepmon.rpmsgdev ||
			g_adspsleepmon.adsp_version <= 1) {
		pr_err("ADSP version doesn't support panic\n");
		return -EINVAL;
	}

	if (!(val & 0x1))
		g_adspsleepmon.b_config_adsp_panic_lpm = false;
	else
		g_adspsleepmon.b_config_adsp_panic_lpm = true;
	if (!(val & 0x2))
		g_adspsleepmon.b_config_adsp_panic_lpi = false;
	else
		g_adspsleepmon.b_config_adsp_panic_lpi = true;
	if (!(val & 0x4))
		g_adspsleepmon.b_config_adsp_panic_lpm_overall = false;
	else
		g_adspsleepmon.b_config_adsp_panic_lpm_overall = true;
	if (!(val & 0x8))
		g_adspsleepmon.b_config_adsp_ssr_enable = false;
	else {
		if (g_adspsleepmon.adsp_rproc) {
			if (g_adspsleepmon.b_config_adsp_panic_lpm ||
				g_adspsleepmon.b_config_adsp_panic_lpi ||
				g_adspsleepmon.b_config_adsp_panic_lpm_overall)
				g_adspsleepmon.b_config_adsp_ssr_enable = true;
			else {
				pr_err("ADSP panics are not enabled and discarded ADSP SSR enable request\n");
				return -EINVAL;
			}
		} else {
			pr_err("Couldn't find rproc handle for ADSP and discarded ADSP SSR enable request\n");
			return -EINVAL;
		}
	}
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(adsp_panic_state_fops,
			debugfs_adsp_panic_state_read,
			debugfs_adsp_panic_state_write,
			"%u\n");

static int read_adsp_panic_state_show(struct seq_file *s, void *d)
{
	int val = g_adspsleepmon.b_config_adsp_panic_lpm |
			(g_adspsleepmon.b_config_adsp_panic_lpi << 1) |
			(g_adspsleepmon.b_config_adsp_panic_lpm_overall << 2) |
			(g_adspsleepmon.b_config_adsp_ssr_enable << 3);

	if (!g_adspsleepmon.rpmsgdev ||
			g_adspsleepmon.adsp_version <= 1) {
		seq_puts(s, "\nADSP version doesn't support panic\n");
		return 0;
	}

	if (val & 1)
		seq_puts(s, "\nADSP panic on LPM violation enabled\n");
	else
		seq_puts(s, "\nADSP panic on LPM violation is disabled\n");
	if (val & 2)
		seq_puts(s, "\nADSP panic on LPI violation enabled\n");
	else
		seq_puts(s, "\nADSP panic on LPI violation is disabled\n");
	if (val & 4)
		seq_puts(s, "\nADSP panic for LPM overall violation enabled\n");
	else
		seq_puts(s, "\nADSP panic for LPM overall violation is disabled\n");
	if (val & 8)
		seq_puts(s, "\nADSP SSR config enabled\n");
	else
		seq_puts(s, "\nADSP SSR config disabled\n");

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(read_adsp_panic_state);

static void print_complete_dsppm_info(void)
{
	int i;

	if (g_adspsleepmon.dsppm_clients.result ==
			DSPPM_CLIENT_DATA_FETCH_SUCCESS) {
		pr_err("TS:0x%llX,AC:%u,TMIPS:%u,TMPPS:%u,QCLK:%u,KF:%u,DDRAB:%u,DDRIB:%u,SNOCAB:%u,SNOCIB:%u,AL:%u,SL:%u,AHBE:%u,AHBI:%u,ASL:%u\n",
		g_adspsleepmon.dsppm_clients.timestamp, g_adspsleepmon.dsppm_clients.num_clients,
		g_adspsleepmon.dsppm_clients.agg_data.agg_mips.mips,
		g_adspsleepmon.dsppm_clients.agg_data.agg_mips.mpps,
		g_adspsleepmon.dsppm_clients.agg_data.agg_mips.clock,
		g_adspsleepmon.dsppm_clients.agg_data.agg_mips.kfactor,
		g_adspsleepmon.dsppm_clients.agg_data.agg_bw.adsp_ddr_ab,
		g_adspsleepmon.dsppm_clients.agg_data.agg_bw.adsp_ddr_ib,
		g_adspsleepmon.dsppm_clients.agg_data.agg_bw.snoc_ddr_ab,
		g_adspsleepmon.dsppm_clients.agg_data.agg_bw.snoc_ddr_ib,
		g_adspsleepmon.dsppm_clients.agg_data.agg_bw.ddr_latency,
		g_adspsleepmon.dsppm_clients.agg_data.agg_bw.snoc_latency,
		g_adspsleepmon.dsppm_clients.agg_data.agg_ahb.ahbe_hz,
		g_adspsleepmon.dsppm_clients.agg_data.agg_ahb.ahbi_hz,
		g_adspsleepmon.dsppm_clients.agg_data.agg_latency);

		for (i = 0; i < g_adspsleepmon.dsppm_clients.num_clients; i++) {
			pr_err("%u:N:%s,ID:%u,RTS:%u,TMIPS:%u,MIPSPT:%u,TMPPS:%u,ADSPFCLK:%u,BPS:%u,UP:%u,SL:%u,POW:%u,PD:%s,ISP:%u\n",
			i, g_adspsleepmon.dsppm_clients.clients[i].clientname,
			g_adspsleepmon.dsppm_clients.clients[i].clientid,
			g_adspsleepmon.dsppm_clients.clients[i].timestamp,
			g_adspsleepmon.dsppm_clients.clients[i].mips.total,
			g_adspsleepmon.dsppm_clients.clients[i].mips.per_thread,
			g_adspsleepmon.dsppm_clients.clients[i].mpps.total,
			g_adspsleepmon.dsppm_clients.clients[i].mpps.floor_clock,
			g_adspsleepmon.dsppm_clients.clients[i].bw.bytes_persec,
			g_adspsleepmon.dsppm_clients.clients[i].bw.usage_percentage,
			g_adspsleepmon.dsppm_clients.clients[i].sleeplatency,
			g_adspsleepmon.dsppm_clients.clients[i].poweron,
			g_adspsleepmon.dsppm_clients.clients[i].process,
			g_adspsleepmon.dsppm_clients.clients[i].islandparticipation.islandopt);
		}
	} else if (g_adspsleepmon.dsppm_clients.result ==
			DSPPM_CLIENT_DATA_FAILED_ONGOING_COHERENT)
		pr_err("DSPPM fetch from DSP failed: Ongoing coherent DSPPM data read\n");
	else if (g_adspsleepmon.dsppm_clients.result ==
			DSPPM_CLIENT_DATA_FAILED_DSPPM_RETURNED_NULL)
		pr_err("DSPPM fetch from DSP failed: QDSPPM returned NULL\n");
	else
		pr_err("DSPPM fetch from DSP failed: result = %d\n",
				g_adspsleepmon.dsppm_clients.result);
}

int adsp_sleepmon_log_master_stats(u32 mask)
{
	u64 accumulated;
	int result;

	mask = mask & 0x7;
	if (!mask) {
		pr_err("\nadsp_sleepmon_log_master_stats: Invalid Input Parameter\n");
		return -EINVAL;
	}

	if (mask & 0x1) {
		if (g_adspsleepmon.sysmon_event_stats) {
			pr_info("\nsysMon stats:\n\n");
			pr_info("Core clock(KHz): %d\n",
					g_adspsleepmon.sysmon_event_stats->core_clk);
			pr_info("Ab vote(Bytes): %llu\n",
				(((u64)g_adspsleepmon.sysmon_event_stats->ab_vote_msb << 32) |
						g_adspsleepmon.sysmon_event_stats->ab_vote_lsb));
			pr_info("Ib vote(Bytes): %llu\n",
				(((u64)g_adspsleepmon.sysmon_event_stats->ib_vote_msb << 32) |
						g_adspsleepmon.sysmon_event_stats->ib_vote_lsb));
			pr_info("Sleep latency(usec): %u\n",
				g_adspsleepmon.sysmon_event_stats->sleep_latency > 0 ?
					g_adspsleepmon.sysmon_event_stats->sleep_latency : U32_MAX);
			pr_info("Timestamp: %llu\n",
				(((u64)g_adspsleepmon.sysmon_event_stats->timestamp_msb << 32) |
					g_adspsleepmon.sysmon_event_stats->timestamp_lsb));
		} else {
			pr_err("Sysmon Event Stats are not available\n");
		}
	}

	if (mask & 0x2) {
		if (g_adspsleepmon.dsppm_stats) {
			pr_info("\nDSPPM stats:\n\n");
			pr_info("Version: %u\n", g_adspsleepmon.dsppm_stats->version);
			pr_info("Sleep latency(usec): %u\n",
					g_adspsleepmon.dsppm_stats->latency_us ?
					g_adspsleepmon.dsppm_stats->latency_us : U32_MAX);
			pr_info("Timestamp: %llu\n", g_adspsleepmon.dsppm_stats->timestamp);

			for (int i = 0; i < ADSPSLEEPMON_DSPPMSTATS_NUMPD; i++) {
				pr_info("Pid: %d, Num active clients: %d\n",
						g_adspsleepmon.dsppm_stats->pd[i].pid,
						g_adspsleepmon.dsppm_stats->pd[i].num_active);
			}

			if (g_adspsleepmon.adsp_version) {
				result = sleepmon_get_dsppm_client_stats();

				if (!result) {
					wait_for_completion(&g_adspsleepmon.sem);
					print_complete_dsppm_info();
				}
			}
		} else {
			pr_err("Dsppm Stats are not available\n");
		}
	}

	if (mask & 0x4) {
		if (g_adspsleepmon.lpm_stats) {
			accumulated = g_adspsleepmon.lpm_stats->accumulated;

			if (g_adspsleepmon.lpm_stats->last_entered_at >
						g_adspsleepmon.lpm_stats->last_exited_at)
				accumulated += arch_timer_read_counter() -
							g_adspsleepmon.lpm_stats->last_entered_at;

			pr_info("\nLPM stats:\n\n");
			pr_info("Count = %u\n", g_adspsleepmon.lpm_stats->count);
			pr_info("Last Entered At = %llu\n",
				g_adspsleepmon.lpm_stats->last_entered_at);
			pr_info("Last Exited At = %llu\n",
				g_adspsleepmon.lpm_stats->last_exited_at);
			pr_info("Accumulated Duration = %llu\n", accumulated);
		} else {
			pr_err("LPM Stats are not available\n");
		}

		if (g_adspsleepmon.lpi_stats) {
			accumulated = g_adspsleepmon.lpi_stats->accumulated;

			if (g_adspsleepmon.lpi_stats->last_entered_at >
						g_adspsleepmon.lpi_stats->last_exited_at)
				accumulated += arch_timer_read_counter() -
						g_adspsleepmon.lpi_stats->last_entered_at;

			pr_info("\nLPI stats:\n\n");
			pr_info("Count = %u\n", g_adspsleepmon.lpi_stats->count);
			pr_info("Last Entered At = %llu\n",
				g_adspsleepmon.lpi_stats->last_entered_at);
			pr_info("Last Exited At = %llu\n",
				g_adspsleepmon.lpi_stats->last_exited_at);
			pr_info("Accumulated Duration = %llu\n",
				accumulated);
		} else {
			pr_err("LPI Stats are not available\n");
		}
	}

	return 0;
}
EXPORT_SYMBOL(adsp_sleepmon_log_master_stats);

static int master_stats_show(struct seq_file *s, void *d)
{
	int i = 0;
	u64 accumulated;
	int result;

	if (g_adspsleepmon.adsp_version) {
		result = sleepmon_get_dsppm_client_stats();

		if (!result) {

			wait_for_completion(&g_adspsleepmon.sem);
			print_complete_dsppm_info();
		}
	}
	if (g_adspsleepmon.sysmon_event_stats) {
		seq_puts(s, "\nsysMon stats:\n\n");
		seq_printf(s, "Core clock(KHz): %d\n",
				g_adspsleepmon.sysmon_event_stats->core_clk);
		seq_printf(s, "Ab vote(Bytes): %llu\n",
				(((u64)g_adspsleepmon.sysmon_event_stats->ab_vote_msb << 32) |
					   g_adspsleepmon.sysmon_event_stats->ab_vote_lsb));
		seq_printf(s, "Ib vote(Bytes): %llu\n",
				(((u64)g_adspsleepmon.sysmon_event_stats->ib_vote_msb << 32) |
					   g_adspsleepmon.sysmon_event_stats->ib_vote_lsb));
		seq_printf(s, "Sleep latency(usec): %u\n",
				g_adspsleepmon.sysmon_event_stats->sleep_latency > 0 ?
				g_adspsleepmon.sysmon_event_stats->sleep_latency : U32_MAX);
		seq_printf(s, "Timestamp: %llu\n",
				(((u64)g_adspsleepmon.sysmon_event_stats->timestamp_msb << 32) |
					g_adspsleepmon.sysmon_event_stats->timestamp_lsb));
	}

	if (g_adspsleepmon.dsppm_stats) {
		seq_puts(s, "\nDSPPM stats:\n\n");
		seq_printf(s, "Version: %u\n", g_adspsleepmon.dsppm_stats->version);
		seq_printf(s, "Sleep latency(usec): %u\n",
					g_adspsleepmon.dsppm_stats->latency_us ?
					g_adspsleepmon.dsppm_stats->latency_us : U32_MAX);
		seq_printf(s, "Timestamp: %llu\n", g_adspsleepmon.dsppm_stats->timestamp);

		for (; i < ADSPSLEEPMON_DSPPMSTATS_NUMPD; i++) {
			seq_printf(s, "Pid: %d, Num active clients: %d\n",
						g_adspsleepmon.dsppm_stats->pd[i].pid,
						g_adspsleepmon.dsppm_stats->pd[i].num_active);
		}
	}

	if (g_adspsleepmon.lpm_stats) {
		accumulated = g_adspsleepmon.lpm_stats->accumulated;

		if (g_adspsleepmon.lpm_stats->last_entered_at >
					g_adspsleepmon.lpm_stats->last_exited_at)
			accumulated += arch_timer_read_counter() -
						g_adspsleepmon.lpm_stats->last_entered_at;

		seq_puts(s, "\nLPM stats:\n\n");
		seq_printf(s, "Count = %u\n", g_adspsleepmon.lpm_stats->count);
		seq_printf(s, "Last Entered At = %llu\n",
			g_adspsleepmon.lpm_stats->last_entered_at);
		seq_printf(s, "Last Exited At = %llu\n",
			g_adspsleepmon.lpm_stats->last_exited_at);
		seq_printf(s, "Accumulated Duration = %llu\n", accumulated);
	}

	if (g_adspsleepmon.lpi_stats) {
		accumulated = g_adspsleepmon.lpi_stats->accumulated;

		if (g_adspsleepmon.lpi_stats->last_entered_at >
					g_adspsleepmon.lpi_stats->last_exited_at)
			accumulated += arch_timer_read_counter() -
					g_adspsleepmon.lpi_stats->last_entered_at;

		seq_puts(s, "\nLPI stats:\n\n");
		seq_printf(s, "Count = %u\n", g_adspsleepmon.lpi_stats->count);
		seq_printf(s, "Last Entered At = %llu\n",
			g_adspsleepmon.lpi_stats->last_entered_at);
		seq_printf(s, "Last Exited At = %llu\n",
			g_adspsleepmon.lpi_stats->last_exited_at);
		seq_printf(s, "Accumulated Duration = %llu\n",
			accumulated);
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(master_stats);

static void adspsleepmon_timer_cb(struct timer_list *unused)
{
	/*
	 * Configured timer has fired, wakeup the kernel thread
	 */
	g_adspsleepmon.timer_event = true;
	wake_up_interruptible(&adspsleepmon_wq);
}

static void sleepmon_get_dsppm_clients(void)
{
	int result = 0;

	if (g_adspsleepmon.adsp_version) {
		result = sleepmon_get_dsppm_client_stats();

		if (!result) {
			if (!wait_for_completion_timeout(&g_adspsleepmon.sem,
						WAIT_FOR_DSPPM_CLIENTDATA_TIMEOUT)) {
				pr_err("timeout waiting for completion\n");
			}
			print_complete_dsppm_info();
		} else
			pr_err("sleepmon_get_dsppm_client_stats failed with result: %u\n", result);
	}
}

static bool sleepmon_is_audio_active(struct dsppm_stats *curr_dsppm_stats)
{
	int i;
	bool is_audio_active = false;
	bool is_pid_audio = false;

	for (i = 0; i < ADSPSLEEPMON_DSPPMSTATS_NUMPD; i++) {

		if (curr_dsppm_stats->pd[i].pid &&
		(curr_dsppm_stats->pd[i].num_active > 0)) {

			if ((curr_dsppm_stats->pd[i].pid &
				ADSPSLEEPMON_DSPPMSTATS_PID_FILTER) ==
				ADSPSLEEPMON_DSPPMSTATS_AUDIO_PID) {
				is_pid_audio = true;
				is_audio_active = true;
			} else {
				is_pid_audio = false;
			}

			pr_err("ADSP PID: %d (isAudio? %d), active clients: %d\n",
				curr_dsppm_stats->pd[i].pid,
				is_pid_audio,
				curr_dsppm_stats->pd[i].num_active);
		}
	}

	return is_audio_active;
}

static void adspsleepmon_lpm_adsp_panic(void)
{
	if (g_adspsleepmon.b_config_adsp_panic_lpm) {
		pr_err("Sending panic command to ADSP for LPM violation\n");
		sleepmon_send_ssr_command();
	} else if (g_adspsleepmon.b_panic_lpm) {
		panic("ADSP sleep issue detected");
	}
}

static void adspsleepmon_lpm_adsp_panic_overall(void)
{
	if (g_adspsleepmon.b_config_adsp_panic_lpm_overall) {

		pr_err("Sending panic command to ADSP for LPM violation, monitored duration (msec): %u, num resumes: %u\n",
			(g_adspsleepmon.accumulated_duration /
				ADSPSLEEPMON_SYS_CLK_TICKS_PER_MILLISEC),
			g_adspsleepmon.accumulated_resumes);

		sleepmon_send_ssr_command();
	}
}

static void sleepmon_lpm_exception_check(u64 curr_timestamp, u64 elapsed_time)
{
	struct sleep_stats curr_lpm_stats;
	struct dsppm_stats curr_dsppm_stats;
	struct sysmon_event_stats sysmon_event_stats;
	bool is_audio_active = false;

	/*
	 * Read ADSP sleep statistics and
	 * see if ADSP has entered sleep.
	 */
	memcpy(&curr_lpm_stats,
			g_adspsleepmon.lpm_stats,
	sizeof(struct sleep_stats));

	/*
	 * Check if ADSP didn't power collapse post
	 * no active client.
	 */
	if ((!curr_lpm_stats.count) ||
		(curr_lpm_stats.last_exited_at >
			curr_lpm_stats.last_entered_at) ||
		((curr_lpm_stats.last_exited_at <
			curr_lpm_stats.last_entered_at) &&
		(curr_lpm_stats.last_entered_at >= curr_timestamp))) {

		memcpy(&curr_dsppm_stats,
			g_adspsleepmon.dsppm_stats,
		sizeof(struct dsppm_stats));

		memcpy(&sysmon_event_stats,
			g_adspsleepmon.sysmon_event_stats,
			sizeof(struct sysmon_event_stats));

		if (curr_lpm_stats.accumulated ==
				g_adspsleepmon.backup_lpm_stats.accumulated) {

			pr_err("Detected ADSP sleep issue:\n");
			pr_err("ADSP clock: %u, sleep latency: %u\n",
					sysmon_event_stats.core_clk,
					sysmon_event_stats.sleep_latency);
			pr_err("Monitored duration (msec):%u,Sleep duration(msec): %u\n",
				(elapsed_time /
				ADSPSLEEPMON_SYS_CLK_TICKS_PER_MILLISEC),
				((curr_lpm_stats.accumulated -
				g_adspsleepmon.backup_lpm_stats.accumulated) /
				ADSPSLEEPMON_SYS_CLK_TICKS_PER_MILLISEC));

			is_audio_active = sleepmon_is_audio_active(&curr_dsppm_stats);
			sleepmon_get_dsppm_clients();

			if ((g_adspsleepmon.b_panic_lpm ||
				g_adspsleepmon.b_config_adsp_panic_lpm) &&
				is_audio_active)
				adspsleepmon_lpm_adsp_panic();
			else {
				g_adspsleepmon.accumulated_duration += elapsed_time;

				if (g_adspsleepmon.suspend_event)
					g_adspsleepmon.accumulated_resumes++;

				if ((g_adspsleepmon.accumulated_duration >=
					((u64)g_adspsleepmon.lpm_wait_time_overall *
						ADSPSLEEPMON_SYS_CLK_TICKS_PER_SEC)) &&
					(g_adspsleepmon.accumulated_resumes >=
						g_adspsleepmon.min_required_resumes)) {
					adspsleepmon_lpm_adsp_panic_overall();
					g_adspsleepmon.accumulated_duration = 0;
					g_adspsleepmon.accumulated_resumes = 0;
				}
			}
		} else {
			g_adspsleepmon.accumulated_duration = 0;
			g_adspsleepmon.accumulated_resumes = 0;
		}

	}

	memcpy(&g_adspsleepmon.backup_lpm_stats,
		&curr_lpm_stats,
		sizeof(struct sleep_stats));

	g_adspsleepmon.backup_lpm_timestamp = __arch_counter_get_cntvct();
}

static void sleepmon_lpi_exception_check(u64 curr_timestamp, u64 elapsed_time)
{
	struct sleep_stats curr_lpi_stats;
	struct dsppm_stats curr_dsppm_stats;
	struct sysmon_event_stats sysmon_event_stats;
	bool is_audio_active = false;

	/*
	 * Read ADSP LPI statistics and see
	 * if ADSP is in LPI state.
	 */
	memcpy(&curr_lpi_stats,
			g_adspsleepmon.lpi_stats,
			sizeof(struct sleep_stats));

	/*
	 * Check if ADSP is not in LPI
	 */
	if ((!curr_lpi_stats.count) ||
		(curr_lpi_stats.last_exited_at >
				curr_lpi_stats.last_entered_at) ||
		((curr_lpi_stats.last_exited_at <
				curr_lpi_stats.last_entered_at) &&
		 (curr_lpi_stats.last_entered_at >= curr_timestamp))) {

		memcpy(&curr_dsppm_stats,
			g_adspsleepmon.dsppm_stats,
			sizeof(struct dsppm_stats));

		memcpy(&sysmon_event_stats,
			g_adspsleepmon.sysmon_event_stats,
			sizeof(struct sysmon_event_stats));

		if (curr_lpi_stats.accumulated ==
			g_adspsleepmon.backup_lpi_stats.accumulated) {

			pr_err("Detected ADSP LPI issue:\n");
			pr_err("ADSP clock: %u, sleep latency: %u\n",
					sysmon_event_stats.core_clk,
					sysmon_event_stats.sleep_latency);
			pr_err("Monitored duration (msec):%u,LPI duration(msec): %u\n",
				(elapsed_time /
				ADSPSLEEPMON_SYS_CLK_TICKS_PER_MILLISEC),
				((curr_lpi_stats.accumulated -
				g_adspsleepmon.backup_lpi_stats.accumulated) /
				ADSPSLEEPMON_SYS_CLK_TICKS_PER_MILLISEC));

			is_audio_active = sleepmon_is_audio_active(&curr_dsppm_stats);
			sleepmon_get_dsppm_clients();
			sleepmon_send_lpi_issue_command();
		}
	}

	memcpy(&g_adspsleepmon.backup_lpi_stats,
		&curr_lpi_stats,
		sizeof(struct sleep_stats));

	g_adspsleepmon.backup_lpi_timestamp = __arch_counter_get_cntvct();
}

static int adspsleepmon_worker(void *data)
{
	u64 curr_timestamp, elapsed_time;
	int result = 0;

	while (!kthread_should_stop()) {
		result = wait_event_interruptible(adspsleepmon_wq,
					(kthread_should_stop() ||
					g_adspsleepmon.timer_event ||
					g_adspsleepmon.suspend_event));

		if (kthread_should_stop())
			break;

		if (result)
			continue;


		pr_info("timer_event = %d,suspend_event = %d\n",
				g_adspsleepmon.timer_event,
				g_adspsleepmon.suspend_event);

		/*
		 * Handle timer event.
		 * 2 cases:
		 * - There is no active use case on ADSP, we
		 *   are expecting it to be in power collapse
		 * - There is an active LPI use case on ADSP,
		 *   we are expecting it to be in LPI.
		 *
		 * Critical section start
		 */
		mutex_lock(&g_adspsleepmon.lock);

		if (!g_adspsleepmon.audio_stats.num_sessions) {

			curr_timestamp = __arch_counter_get_cntvct();

			if (curr_timestamp >= g_adspsleepmon.backup_lpm_timestamp)
				elapsed_time = (curr_timestamp -
					 g_adspsleepmon.backup_lpm_timestamp);
			else
				elapsed_time = U64_MAX -
					g_adspsleepmon.backup_lpm_timestamp +
					curr_timestamp;

			if (!g_adspsleepmon.timer_event && g_adspsleepmon.suspend_event) {
				/*
				 * Check if we have elapsed enough duration
				 * to make a decision if it is not timer
				 * event
				 */
				if (elapsed_time <
					(g_adspsleepmon.lpm_wait_time *
					ADSPSLEEPMON_SYS_CLK_TICKS_PER_SEC)) {
					mutex_unlock(&g_adspsleepmon.lock);
					g_adspsleepmon.suspend_event = false;
					continue;
				}
			}

			sleepmon_lpm_exception_check(curr_timestamp, elapsed_time);

		} else if (g_adspsleepmon.audio_stats.num_sessions ==
					g_adspsleepmon.audio_stats.num_lpi_sessions) {

			curr_timestamp = __arch_counter_get_cntvct();

			if (curr_timestamp >=
					g_adspsleepmon.backup_lpi_timestamp)
				elapsed_time = (curr_timestamp -
						g_adspsleepmon.backup_lpi_timestamp);
			else
				elapsed_time = U64_MAX -
						g_adspsleepmon.backup_lpi_timestamp +
						curr_timestamp;

			if (!g_adspsleepmon.timer_event && g_adspsleepmon.suspend_event) {
				/*
				 * Check if we have elapsed enough duration
				 * to make a decision if it is not timer
				 * event
				 */


				if (elapsed_time <
					(g_adspsleepmon.lpi_wait_time *
					ADSPSLEEPMON_SYS_CLK_TICKS_PER_SEC)) {
					mutex_unlock(&g_adspsleepmon.lock);
					g_adspsleepmon.suspend_event = false;
					continue;
				}
			}

			sleepmon_lpi_exception_check(curr_timestamp, elapsed_time);
		}

		if (g_adspsleepmon.timer_event) {
			g_adspsleepmon.timer_event = false;
			g_adspsleepmon.timer_pending = false;
		}

		g_adspsleepmon.suspend_event = false;

		mutex_unlock(&g_adspsleepmon.lock);
	}

	return 0;
}

static int adspsleepmon_device_open(struct inode *inode, struct file *fp)
{
	struct adspsleepmon_file *fl = NULL;

	/*
	 * Check if SMEM side needs initialization
	 */
	if (!g_adspsleepmon.smem_init_done)
		adspsleepmon_smem_init();

	if (!g_adspsleepmon.smem_init_done)
		return -ENODEV;

	/*
	 * Check for device minor and return error if not matching
	 * May need to allocate (kzalloc) based on requirement and update
	 * fp->private_data.
	 */

	fl = kzalloc(sizeof(*fl), GFP_KERNEL);
	if (IS_ERR_OR_NULL(fl))
		return -ENOMEM;

	INIT_HLIST_NODE(&fl->hn);
	fl->num_sessions = 0;
	fl->num_lpi_sessions = 0;
	fl->b_connected = 0;
	spin_lock_init(&fl->hlock);
	fp->private_data = fl;
	return 0;
}

static int adspsleepmon_device_release(struct inode *inode, struct file *fp)
{
	struct adspsleepmon_file *fl = (struct adspsleepmon_file *)fp->private_data;
	u32 num_sessions = 0, num_lpi_sessions = 0, delay = 0;
	struct adspsleepmon_file *client = NULL;
	struct hlist_node *n;

	/*
	 * do tear down
	 */
	if (fl) {
		/*
		 * Critical section start
		 */
		mutex_lock(&g_adspsleepmon.lock);
		spin_lock(&fl->hlock);
		hlist_del_init(&fl->hn);

		/*
		 * Reaggregate num sessions
		 */
		hlist_for_each_entry_safe(client, n,
					&g_adspsleepmon.audio_clients, hn) {
			if (client->b_connected) {
				num_sessions += client->num_sessions;
				num_lpi_sessions += client->num_lpi_sessions;
			}
		}

		spin_unlock(&fl->hlock);
		kfree(fl);

		/*
		 * Start/stop the timer based on
		 *   Start -> No active session (from previous
		 *				   active session)
		 *   Stop -> An active session
		 */
		if (num_sessions != g_adspsleepmon.audio_stats.num_sessions) {
			if (!num_sessions ||
					(num_sessions == num_lpi_sessions)) {

				if (!num_sessions) {
					memcpy(&g_adspsleepmon.backup_lpm_stats,
							g_adspsleepmon.lpm_stats,
							sizeof(struct sleep_stats));
					g_adspsleepmon.backup_lpm_timestamp =
						__arch_counter_get_cntvct();
					delay = g_adspsleepmon.lpm_wait_time;
				} else {
					memcpy(&g_adspsleepmon.backup_lpi_stats,
							g_adspsleepmon.lpi_stats,
							sizeof(struct sleep_stats));
					g_adspsleepmon.backup_lpi_timestamp =
						__arch_counter_get_cntvct();
					delay = g_adspsleepmon.lpi_wait_time;
					g_adspsleepmon.accumulated_duration = 0;
					g_adspsleepmon.accumulated_resumes = 0;
				}

				mod_timer(&adspsleep_timer, jiffies + delay * HZ);
				g_adspsleepmon.timer_pending = true;
			} else if (g_adspsleepmon.timer_pending) {
				del_timer(&adspsleep_timer);
				g_adspsleepmon.timer_pending = false;
				g_adspsleepmon.accumulated_duration = 0;
				g_adspsleepmon.accumulated_resumes = 0;
			}

			g_adspsleepmon.audio_stats.num_sessions = num_sessions;
			g_adspsleepmon.audio_stats.num_lpi_sessions = num_lpi_sessions;
		}

		mutex_unlock(&g_adspsleepmon.lock);

		pr_info("Release: num_sessions=%d,num_lpi_sessions=%d,timer_pending=%d\n",
						g_adspsleepmon.audio_stats.num_sessions,
						g_adspsleepmon.audio_stats.num_lpi_sessions,
						g_adspsleepmon.timer_pending);
		/*
		 * Critical section Done
		 */
	} else {
		return -ENODATA;
	}

	return 0;
}

static long adspsleepmon_device_ioctl(struct file *file,
								unsigned int ioctl_num,
								unsigned long ioctl_param)
{
	int ret = 0;

	struct adspsleepmon_file *fl = (struct adspsleepmon_file *)file->private_data;

	switch (ioctl_num) {

	case ADSPSLEEPMON_IOCTL_CONFIGURE_PANIC:
	{
		struct adspsleepmon_ioctl_panic panic_param;

		if (copy_from_user(&panic_param, (void const __user *)ioctl_param,
					sizeof(struct adspsleepmon_ioctl_panic))) {
			ret = -ENOTTY;
			pr_err("IOCTL copy from user failed\n");
			goto bail;
		}

		if (panic_param.version <
			ADSPSLEEPMON_IOCTL_CONFIG_PANIC_VER_1) {
			ret = -ENOTTY;
			pr_err("Bad version (%d) in IOCTL (%d)\n",
					panic_param.version, ioctl_num);
			goto bail;
		}

		if (panic_param.command >= ADSPSLEEPMON_RESET_PANIC_MAX) {
			ret = -EINVAL;
			pr_err("Invalid command (%d) passed in IOCTL (%d)\n",
					panic_param.command, ioctl_num);
			goto bail;
		}

		switch (panic_param.command) {
		case ADSPSLEEPMON_DISABLE_PANIC_LPM:
			g_adspsleepmon.b_panic_lpm = false;
		break;

		case ADSPSLEEPMON_DISABLE_PANIC_LPI:
			g_adspsleepmon.b_panic_lpi = false;
		break;

		case ADSPSLEEPMON_RESET_PANIC_LPM:
			g_adspsleepmon.b_panic_lpm =
						g_adspsleepmon.b_config_panic_lpm;
		break;

		case ADSPSLEEPMON_RESET_PANIC_LPI:
			g_adspsleepmon.b_panic_lpi =
						g_adspsleepmon.b_config_panic_lpi;
		break;
		}
	}
	break;

	case ADSPSLEEPMON_IOCTL_AUDIO_ACTIVITY:
	{

		struct adspsleepmon_ioctl_audio audio_param;
		u32 num_sessions = 0, num_lpi_sessions = 0, delay = 0;
		struct adspsleepmon_file *client = NULL;
		struct hlist_node *n;

		if (copy_from_user(&audio_param, (void const __user *)ioctl_param,
					sizeof(struct adspsleepmon_ioctl_audio))) {
			ret = -ENOTTY;
			pr_err("IOCTL copy from user failed\n");
			goto bail;
		}
		if (!fl) {
			pr_err("bad pointer to private data in ioctl\n");
			ret = -ENOMEM;

			goto bail;
		}

		if (fl->b_connected &&
				(fl->b_connected != ADSPSLEEPMON_AUDIO_CLIENT)) {
			pr_err("Restricted IOCTL (%d) called from %d client\n",
					ioctl_num, fl->b_connected);
			ret = -ENOMSG;

			goto bail;
		}

		/*
		 * Check version
		 */
		if (audio_param.version <
					ADSPSLEEPMON_IOCTL_AUDIO_VER_1) {
			ret = -ENOTTY;
			pr_err("Bad version (%d) in IOCTL (%d)\n",
					audio_param.version, ioctl_num);
			goto bail;
		}

		if (audio_param.command >= ADSPSLEEPMON_AUDIO_ACTIVITY_MAX) {
			ret = -EINVAL;
			pr_err("Invalid command (%d) passed in IOCTL (%d)\n",
					audio_param.command, ioctl_num);
			goto bail;
		}

		/*
		 * Critical section start
		 */
		mutex_lock(&g_adspsleepmon.lock);

		if (!fl->b_connected) {
			hlist_add_head(&fl->hn, &g_adspsleepmon.audio_clients);
			fl->b_connected = ADSPSLEEPMON_AUDIO_CLIENT;
		}

		switch (audio_param.command) {
		case ADSPSLEEPMON_AUDIO_ACTIVITY_LPI_START:
			fl->num_lpi_sessions++;
			__attribute__((__fallthrough__));
		case ADSPSLEEPMON_AUDIO_ACTIVITY_START:
			fl->num_sessions++;
		break;

		case ADSPSLEEPMON_AUDIO_ACTIVITY_LPI_STOP:
			if (fl->num_lpi_sessions)
				fl->num_lpi_sessions--;
			else
				pr_info("Received AUDIO LPI activity stop when none active!\n");
			__attribute__((__fallthrough__));
		case ADSPSLEEPMON_AUDIO_ACTIVITY_STOP:
			if (fl->num_sessions)
				fl->num_sessions--;
			else
				pr_info("Received AUDIO activity stop when none active!\n");
		break;

		case ADSPSLEEPMON_AUDIO_ACTIVITY_RESET:
			fl->num_sessions = 0;
			fl->num_lpi_sessions = 0;
		break;
		}

		/*
		 * Iterate over the registered audio IOCTL clients and
		 * calculate total number of active sessions and LPI sessions
		 */
		spin_lock(&fl->hlock);

		hlist_for_each_entry_safe(client, n,
					&g_adspsleepmon.audio_clients, hn) {
			if (client->b_connected) {
				num_sessions += client->num_sessions;
				num_lpi_sessions += client->num_lpi_sessions;
			}
		}

		spin_unlock(&fl->hlock);

		/*
		 * Start/stop the timer based on
		 *   Start -> No active session (from previous
		 *				   active session)
		 *   Stop -> An active session
		 */
		if (!num_sessions ||
				(num_sessions == num_lpi_sessions)) {

			if (!num_sessions) {
				memcpy(&g_adspsleepmon.backup_lpm_stats,
						g_adspsleepmon.lpm_stats,
						sizeof(struct sleep_stats));
				g_adspsleepmon.backup_lpm_timestamp =
						__arch_counter_get_cntvct();
				delay = g_adspsleepmon.lpm_wait_time;
			} else {
				memcpy(&g_adspsleepmon.backup_lpi_stats,
						g_adspsleepmon.lpi_stats,
						sizeof(struct sleep_stats));
				g_adspsleepmon.backup_lpi_timestamp =
						__arch_counter_get_cntvct();
				delay = g_adspsleepmon.lpi_wait_time;
				g_adspsleepmon.accumulated_duration = 0;
				g_adspsleepmon.accumulated_resumes = 0;
			}

			mod_timer(&adspsleep_timer, jiffies + delay * HZ);
			g_adspsleepmon.timer_pending = true;
		} else if (g_adspsleepmon.timer_pending) {
			del_timer(&adspsleep_timer);
			g_adspsleepmon.timer_pending = false;
			g_adspsleepmon.accumulated_duration = 0;
			g_adspsleepmon.accumulated_resumes = 0;
		}

		g_adspsleepmon.audio_stats.num_sessions = num_sessions;
		g_adspsleepmon.audio_stats.num_lpi_sessions = num_lpi_sessions;

		pr_info("Audio: num_sessions=%d,num_lpi_sessions=%d,timer_pending=%d\n",
						g_adspsleepmon.audio_stats.num_sessions,
						g_adspsleepmon.audio_stats.num_lpi_sessions,
						g_adspsleepmon.timer_pending);

		mutex_unlock(&g_adspsleepmon.lock);
		/*
		 * Critical section end
		 */
	}
		break;

	default:
		ret = -ENOTTY;
		pr_info("Unidentified ioctl %d!\n", ioctl_num);
		break;
	}
bail:
	return ret;
}

static const struct file_operations fops = {
	.open = adspsleepmon_device_open,
	.release = adspsleepmon_device_release,
	.unlocked_ioctl = adspsleepmon_device_ioctl,
	.compat_ioctl = adspsleepmon_device_ioctl,
};

static const struct of_device_id sleepmon_rpmsg_of_match[] = {
	{ .compatible = "qcom,msm-adspsleepmon-rpmsg" },
	{ },
};
MODULE_DEVICE_TABLE(of, sleepmon_rpmsg_of_match);

static const struct rpmsg_device_id sleepmon_rpmsg_match[] = {
	{ "sleepmonglink-apps-adsp" },
	{ },
};

static int sleepmon_rpmsg_probe(struct rpmsg_device *dev)
{
	/* Populate child nodes as platform devices */
	of_platform_populate(dev->dev.of_node, NULL, NULL, &dev->dev);
	g_adspsleepmon.rpmsgdev = dev;


	if (!g_adspsleepmon.adsp_rproc &&
			g_adspsleepmon.adsp_rproc_phandle) {
		g_adspsleepmon.adsp_rproc = rproc_get_by_phandle(
				g_adspsleepmon.adsp_rproc_phandle);
	}

	if (g_adspsleepmon.adsp_rproc) {
		pr_info("Resetting recovery flag for ADSP SSR\n");
		qcom_rproc_update_recovery_status(
				g_adspsleepmon.adsp_rproc, false);
	}

	return adspsleepmon_smem_init();
}

static void sleepmon_rpmsg_remove(struct rpmsg_device *dev)
{
	g_adspsleepmon.rpmsgdev = NULL;
	g_adspsleepmon.adsp_version = 0;
}

static struct rpmsg_driver sleepmon_rpmsg_client = {
	.id_table = sleepmon_rpmsg_match,
	.probe = sleepmon_rpmsg_probe,
	.remove = sleepmon_rpmsg_remove,
	.callback = sleepmon_rpmsg_callback,
	.drv = {
		.name = "qcom,msm_adspsleepmon_rpmsg",
		.of_match_table = sleepmon_rpmsg_of_match,
	},
};

static int adspsleepmon_driver_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = 0;
	struct adspsleepmon *me = &g_adspsleepmon;

	mutex_init(&g_adspsleepmon.lock);
	/*
	 * Initialize dtsi config
	 */
	g_adspsleepmon.lpm_wait_time = ADSPSLEEPMON_LPM_WAIT_TIME;
	g_adspsleepmon.lpi_wait_time = ADSPSLEEPMON_LPI_WAIT_TIME;
	g_adspsleepmon.lpm_wait_time_overall = ADSPSLEEPMON_LPM_WAIT_TIME_OVERALL;
	g_adspsleepmon.min_required_resumes = ADSPSLEEPMON_MIN_REQUIRED_RESUMES;
	g_adspsleepmon.b_config_panic_lpm = false;
	g_adspsleepmon.b_config_panic_lpi = false;

	g_adspsleepmon.worker_task = kthread_run(adspsleepmon_worker,
					NULL, "adspsleepmon-worker");

	if (!g_adspsleepmon.worker_task) {
		dev_err(dev, "Failed to create kernel thread\n");
		return -ENOMEM;
	}

	INIT_HLIST_HEAD(&g_adspsleepmon.audio_clients);
	ret = alloc_chrdev_region(&me->devno, 0, 1, ADSPSLEEPMON_DEVICE_NAME_LOCAL);

	if (ret != 0) {
		dev_err(dev, "Failed to allocate char device region\n");
		goto rpmsg_bail;
	}

	cdev_init(&me->cdev, &fops);
	me->cdev.owner = THIS_MODULE;
	ret = cdev_add(&me->cdev, MKDEV(MAJOR(me->devno), 0), 1);

	if (ret != 0) {
		dev_err(dev, "Failed to add cdev\n");
		goto cdev_bail;
	}

	me->class = class_create(THIS_MODULE, "adspsleepmon");

	if (IS_ERR(me->class)) {
		dev_err(dev, "Failed to create a class\n");
		goto class_bail;
	}

	me->dev = device_create(me->class, NULL,
			MKDEV(MAJOR(me->devno), 0), NULL, ADSPSLEEPMON_DEVICE_NAME_LOCAL);

	if (IS_ERR_OR_NULL(me->dev)) {
		dev_err(dev, "Failed to create a device\n");
		goto device_bail;
	}

	g_adspsleepmon.debugfs_dir = debugfs_create_dir("adspsleepmon", NULL);

	if (!g_adspsleepmon.debugfs_dir) {
		dev_err(dev, "Failed to create debugfs directory for adspsleepmon\n");
		goto debugfs_bail;
	}

	g_adspsleepmon.b_config_panic_lpm = of_property_read_bool(dev->of_node,
			"qcom,enable_panic_lpm");

	g_adspsleepmon.b_config_panic_lpi = of_property_read_bool(dev->of_node,
			"qcom,enable_panic_lpi");

	g_adspsleepmon.b_config_adsp_panic_lpm = of_property_read_bool(dev->of_node,
			"qcom,enable_adsp_panic_lpm");

	g_adspsleepmon.b_config_adsp_panic_lpi = of_property_read_bool(dev->of_node,
			"qcom,enable_adsp_panic_lpi");

	g_adspsleepmon.b_config_adsp_panic_lpm_overall = of_property_read_bool(dev->of_node,
			"qcom,enable_adsp_panic_lpm_overall");

	of_property_read_u32(dev->of_node, "qcom,wait_time_lpm",
						 &g_adspsleepmon.lpm_wait_time);

	of_property_read_u32(dev->of_node, "qcom,wait_time_lpi",
						 &g_adspsleepmon.lpi_wait_time);

	of_property_read_u32(dev->of_node, "qcom,wait_time_lpm_overall",
						 &g_adspsleepmon.lpm_wait_time_overall);

	of_property_read_u32(dev->of_node, "qcom,min_required_resumes",
						 &g_adspsleepmon.min_required_resumes);

	of_property_read_u32(dev->of_node, "qcom,rproc-handle",
				&g_adspsleepmon.adsp_rproc_phandle);

	if (g_adspsleepmon.adsp_rproc_phandle &&
		(g_adspsleepmon.b_config_adsp_panic_lpm ||
		 g_adspsleepmon.b_config_adsp_panic_lpi ||
		 g_adspsleepmon.b_config_adsp_panic_lpm_overall)) {
		g_adspsleepmon.b_config_adsp_ssr_enable = true;
		dev_info(dev, "ADSP SSR config enabled\n");
	}

	g_adspsleepmon.b_panic_lpm = g_adspsleepmon.b_config_panic_lpm;
	g_adspsleepmon.b_panic_lpi = g_adspsleepmon.b_config_panic_lpi;

	if (g_adspsleepmon.b_config_panic_lpm ||
			g_adspsleepmon.b_config_panic_lpi) {
		g_adspsleepmon.debugfs_panic_file =
				debugfs_create_file("panic-state",
				 0644, g_adspsleepmon.debugfs_dir, NULL, &panic_state_fops);

		if (!g_adspsleepmon.debugfs_panic_file)
			dev_err(dev, "Unable to create file in debugfs\n");
	}

	g_adspsleepmon.debugfs_read_panic_state =
			debugfs_create_file("read_panic_state",
			 0444, g_adspsleepmon.debugfs_dir, NULL, &read_panic_state_fops);

	if (!g_adspsleepmon.debugfs_read_panic_state)
		dev_err(dev, "Unable to create read panic state file in debugfs\n");

	g_adspsleepmon.debugfs_master_stats =
			debugfs_create_file("master_stats",
			 0444, g_adspsleepmon.debugfs_dir, NULL, &master_stats_fops);

	if (!g_adspsleepmon.debugfs_master_stats)
		dev_err(dev, "Failed to create debugfs file for master stats\n");

	g_adspsleepmon.debugfs_adsp_panic_file =
			debugfs_create_file("adsp_panic_state",
			0644, g_adspsleepmon.debugfs_dir, NULL, &adsp_panic_state_fops);

	if (!g_adspsleepmon.debugfs_adsp_panic_file)
		dev_err(dev, "Unable to create ADSP panic state file in debugfs\n");

	g_adspsleepmon.debugfs_read_adsp_panic_state =
			debugfs_create_file("read_adsp_panic_state",
			0444, g_adspsleepmon.debugfs_dir, NULL, &read_adsp_panic_state_fops);

	if (!g_adspsleepmon.debugfs_read_adsp_panic_state)
		dev_err(dev, "Unable to create ADSP panic state read file in debugfs\n");

	ret = register_rpmsg_driver(&sleepmon_rpmsg_client);

	if (ret) {
		dev_err(dev, "Failed registering rpmsg driver with return %d\n",
				ret);
		goto rpmsg_bail;
	}
	g_adspsleepmon.b_rpmsg_register = true;
	init_completion(&g_adspsleepmon.sem);


	dev_info(dev, "ADSP sleep monitor probe called\n");
	return 0;

debugfs_bail:
	device_destroy(g_adspsleepmon.class, g_adspsleepmon.cdev.dev);
device_bail:
	class_destroy(me->class);
class_bail:
	cdev_del(&me->cdev);
cdev_bail:
	unregister_chrdev_region(me->devno, 1);
rpmsg_bail:
	return ret;
}

static int adspsleepmon_driver_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	device_destroy(g_adspsleepmon.class, g_adspsleepmon.cdev.dev);
	class_destroy(g_adspsleepmon.class);
	cdev_del(&g_adspsleepmon.cdev);
	unregister_chrdev_region(g_adspsleepmon.devno, 1);
	debugfs_remove_recursive(g_adspsleepmon.debugfs_dir);
	unregister_pm_notifier(&adsp_sleepmon_pm_nb);

	if (g_adspsleepmon.b_rpmsg_register) {
		unregister_rpmsg_driver(&sleepmon_rpmsg_client);
		g_adspsleepmon.rpmsgdev = NULL;
		g_adspsleepmon.adsp_version = 0;
	}

	dev_info(dev, "ADSP sleep monitor remove called\n");
	return 0;
}

static const struct of_device_id adspsleepmon_match_table[] = {
	{ .compatible = "qcom,adsp-sleepmon" },
	{ },
};

static struct platform_driver adspsleepmon = {
	.probe = adspsleepmon_driver_probe,
	.remove = adspsleepmon_driver_remove,
	.driver = {
		.name = "adsp_sleepmon",
		.of_match_table = adspsleepmon_match_table,
	},
};

module_platform_driver(adspsleepmon);

MODULE_LICENSE("GPL");

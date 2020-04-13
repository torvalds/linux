// SPDX-License-Identifier: GPL-2.0-only
/*
 * System Control and Power Interface (SCPI) Message Protocol driver
 *
 * SCPI Message Protocol is used between the System Control Processor(SCP)
 * and the Application Processors(AP). The Message Handling Unit(MHU)
 * provides a mechanism for inter-processor communication between SCP's
 * Cortex M3 and AP.
 *
 * SCP offers control and management of the core/cluster power states,
 * various power domain DVFS including the core/cluster, certain system
 * clocks configuration, thermal sensors and many others.
 *
 * Copyright (C) 2015 ARM Ltd.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitmap.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/printk.h>
#include <linux/pm_opp.h>
#include <linux/scpi_protocol.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/spinlock.h>

#define CMD_ID_MASK		GENMASK(6, 0)
#define CMD_TOKEN_ID_MASK	GENMASK(15, 8)
#define CMD_DATA_SIZE_MASK	GENMASK(24, 16)
#define CMD_LEGACY_DATA_SIZE_MASK	GENMASK(28, 20)
#define PACK_SCPI_CMD(cmd_id, tx_sz)		\
	(FIELD_PREP(CMD_ID_MASK, cmd_id) |	\
	FIELD_PREP(CMD_DATA_SIZE_MASK, tx_sz))
#define PACK_LEGACY_SCPI_CMD(cmd_id, tx_sz)	\
	(FIELD_PREP(CMD_ID_MASK, cmd_id) |	\
	FIELD_PREP(CMD_LEGACY_DATA_SIZE_MASK, tx_sz))

#define CMD_SIZE(cmd)	FIELD_GET(CMD_DATA_SIZE_MASK, cmd)
#define CMD_UNIQ_MASK	(CMD_TOKEN_ID_MASK | CMD_ID_MASK)
#define CMD_XTRACT_UNIQ(cmd)	((cmd) & CMD_UNIQ_MASK)

#define SCPI_SLOT		0

#define MAX_DVFS_DOMAINS	8
#define MAX_DVFS_OPPS		16

#define PROTO_REV_MAJOR_MASK	GENMASK(31, 16)
#define PROTO_REV_MINOR_MASK	GENMASK(15, 0)

#define FW_REV_MAJOR_MASK	GENMASK(31, 24)
#define FW_REV_MINOR_MASK	GENMASK(23, 16)
#define FW_REV_PATCH_MASK	GENMASK(15, 0)

#define MAX_RX_TIMEOUT		(msecs_to_jiffies(30))

enum scpi_error_codes {
	SCPI_SUCCESS = 0, /* Success */
	SCPI_ERR_PARAM = 1, /* Invalid parameter(s) */
	SCPI_ERR_ALIGN = 2, /* Invalid alignment */
	SCPI_ERR_SIZE = 3, /* Invalid size */
	SCPI_ERR_HANDLER = 4, /* Invalid handler/callback */
	SCPI_ERR_ACCESS = 5, /* Invalid access/permission denied */
	SCPI_ERR_RANGE = 6, /* Value out of range */
	SCPI_ERR_TIMEOUT = 7, /* Timeout has occurred */
	SCPI_ERR_NOMEM = 8, /* Invalid memory area or pointer */
	SCPI_ERR_PWRSTATE = 9, /* Invalid power state */
	SCPI_ERR_SUPPORT = 10, /* Not supported or disabled */
	SCPI_ERR_DEVICE = 11, /* Device error */
	SCPI_ERR_BUSY = 12, /* Device busy */
	SCPI_ERR_MAX
};

/* SCPI Standard commands */
enum scpi_std_cmd {
	SCPI_CMD_INVALID		= 0x00,
	SCPI_CMD_SCPI_READY		= 0x01,
	SCPI_CMD_SCPI_CAPABILITIES	= 0x02,
	SCPI_CMD_SET_CSS_PWR_STATE	= 0x03,
	SCPI_CMD_GET_CSS_PWR_STATE	= 0x04,
	SCPI_CMD_SET_SYS_PWR_STATE	= 0x05,
	SCPI_CMD_SET_CPU_TIMER		= 0x06,
	SCPI_CMD_CANCEL_CPU_TIMER	= 0x07,
	SCPI_CMD_DVFS_CAPABILITIES	= 0x08,
	SCPI_CMD_GET_DVFS_INFO		= 0x09,
	SCPI_CMD_SET_DVFS		= 0x0a,
	SCPI_CMD_GET_DVFS		= 0x0b,
	SCPI_CMD_GET_DVFS_STAT		= 0x0c,
	SCPI_CMD_CLOCK_CAPABILITIES	= 0x0d,
	SCPI_CMD_GET_CLOCK_INFO		= 0x0e,
	SCPI_CMD_SET_CLOCK_VALUE	= 0x0f,
	SCPI_CMD_GET_CLOCK_VALUE	= 0x10,
	SCPI_CMD_PSU_CAPABILITIES	= 0x11,
	SCPI_CMD_GET_PSU_INFO		= 0x12,
	SCPI_CMD_SET_PSU		= 0x13,
	SCPI_CMD_GET_PSU		= 0x14,
	SCPI_CMD_SENSOR_CAPABILITIES	= 0x15,
	SCPI_CMD_SENSOR_INFO		= 0x16,
	SCPI_CMD_SENSOR_VALUE		= 0x17,
	SCPI_CMD_SENSOR_CFG_PERIODIC	= 0x18,
	SCPI_CMD_SENSOR_CFG_BOUNDS	= 0x19,
	SCPI_CMD_SENSOR_ASYNC_VALUE	= 0x1a,
	SCPI_CMD_SET_DEVICE_PWR_STATE	= 0x1b,
	SCPI_CMD_GET_DEVICE_PWR_STATE	= 0x1c,
	SCPI_CMD_COUNT
};

/* SCPI Legacy Commands */
enum legacy_scpi_std_cmd {
	LEGACY_SCPI_CMD_INVALID			= 0x00,
	LEGACY_SCPI_CMD_SCPI_READY		= 0x01,
	LEGACY_SCPI_CMD_SCPI_CAPABILITIES	= 0x02,
	LEGACY_SCPI_CMD_EVENT			= 0x03,
	LEGACY_SCPI_CMD_SET_CSS_PWR_STATE	= 0x04,
	LEGACY_SCPI_CMD_GET_CSS_PWR_STATE	= 0x05,
	LEGACY_SCPI_CMD_CFG_PWR_STATE_STAT	= 0x06,
	LEGACY_SCPI_CMD_GET_PWR_STATE_STAT	= 0x07,
	LEGACY_SCPI_CMD_SYS_PWR_STATE		= 0x08,
	LEGACY_SCPI_CMD_L2_READY		= 0x09,
	LEGACY_SCPI_CMD_SET_AP_TIMER		= 0x0a,
	LEGACY_SCPI_CMD_CANCEL_AP_TIME		= 0x0b,
	LEGACY_SCPI_CMD_DVFS_CAPABILITIES	= 0x0c,
	LEGACY_SCPI_CMD_GET_DVFS_INFO		= 0x0d,
	LEGACY_SCPI_CMD_SET_DVFS		= 0x0e,
	LEGACY_SCPI_CMD_GET_DVFS		= 0x0f,
	LEGACY_SCPI_CMD_GET_DVFS_STAT		= 0x10,
	LEGACY_SCPI_CMD_SET_RTC			= 0x11,
	LEGACY_SCPI_CMD_GET_RTC			= 0x12,
	LEGACY_SCPI_CMD_CLOCK_CAPABILITIES	= 0x13,
	LEGACY_SCPI_CMD_SET_CLOCK_INDEX		= 0x14,
	LEGACY_SCPI_CMD_SET_CLOCK_VALUE		= 0x15,
	LEGACY_SCPI_CMD_GET_CLOCK_VALUE		= 0x16,
	LEGACY_SCPI_CMD_PSU_CAPABILITIES	= 0x17,
	LEGACY_SCPI_CMD_SET_PSU			= 0x18,
	LEGACY_SCPI_CMD_GET_PSU			= 0x19,
	LEGACY_SCPI_CMD_SENSOR_CAPABILITIES	= 0x1a,
	LEGACY_SCPI_CMD_SENSOR_INFO		= 0x1b,
	LEGACY_SCPI_CMD_SENSOR_VALUE		= 0x1c,
	LEGACY_SCPI_CMD_SENSOR_CFG_PERIODIC	= 0x1d,
	LEGACY_SCPI_CMD_SENSOR_CFG_BOUNDS	= 0x1e,
	LEGACY_SCPI_CMD_SENSOR_ASYNC_VALUE	= 0x1f,
	LEGACY_SCPI_CMD_COUNT
};

/* List all commands that are required to go through the high priority link */
static int legacy_hpriority_cmds[] = {
	LEGACY_SCPI_CMD_GET_CSS_PWR_STATE,
	LEGACY_SCPI_CMD_CFG_PWR_STATE_STAT,
	LEGACY_SCPI_CMD_GET_PWR_STATE_STAT,
	LEGACY_SCPI_CMD_SET_DVFS,
	LEGACY_SCPI_CMD_GET_DVFS,
	LEGACY_SCPI_CMD_SET_RTC,
	LEGACY_SCPI_CMD_GET_RTC,
	LEGACY_SCPI_CMD_SET_CLOCK_INDEX,
	LEGACY_SCPI_CMD_SET_CLOCK_VALUE,
	LEGACY_SCPI_CMD_GET_CLOCK_VALUE,
	LEGACY_SCPI_CMD_SET_PSU,
	LEGACY_SCPI_CMD_GET_PSU,
	LEGACY_SCPI_CMD_SENSOR_CFG_PERIODIC,
	LEGACY_SCPI_CMD_SENSOR_CFG_BOUNDS,
};

/* List all commands used by this driver, used as indexes */
enum scpi_drv_cmds {
	CMD_SCPI_CAPABILITIES = 0,
	CMD_GET_CLOCK_INFO,
	CMD_GET_CLOCK_VALUE,
	CMD_SET_CLOCK_VALUE,
	CMD_GET_DVFS,
	CMD_SET_DVFS,
	CMD_GET_DVFS_INFO,
	CMD_SENSOR_CAPABILITIES,
	CMD_SENSOR_INFO,
	CMD_SENSOR_VALUE,
	CMD_SET_DEVICE_PWR_STATE,
	CMD_GET_DEVICE_PWR_STATE,
	CMD_MAX_COUNT,
};

static int scpi_std_commands[CMD_MAX_COUNT] = {
	SCPI_CMD_SCPI_CAPABILITIES,
	SCPI_CMD_GET_CLOCK_INFO,
	SCPI_CMD_GET_CLOCK_VALUE,
	SCPI_CMD_SET_CLOCK_VALUE,
	SCPI_CMD_GET_DVFS,
	SCPI_CMD_SET_DVFS,
	SCPI_CMD_GET_DVFS_INFO,
	SCPI_CMD_SENSOR_CAPABILITIES,
	SCPI_CMD_SENSOR_INFO,
	SCPI_CMD_SENSOR_VALUE,
	SCPI_CMD_SET_DEVICE_PWR_STATE,
	SCPI_CMD_GET_DEVICE_PWR_STATE,
};

static int scpi_legacy_commands[CMD_MAX_COUNT] = {
	LEGACY_SCPI_CMD_SCPI_CAPABILITIES,
	-1, /* GET_CLOCK_INFO */
	LEGACY_SCPI_CMD_GET_CLOCK_VALUE,
	LEGACY_SCPI_CMD_SET_CLOCK_VALUE,
	LEGACY_SCPI_CMD_GET_DVFS,
	LEGACY_SCPI_CMD_SET_DVFS,
	LEGACY_SCPI_CMD_GET_DVFS_INFO,
	LEGACY_SCPI_CMD_SENSOR_CAPABILITIES,
	LEGACY_SCPI_CMD_SENSOR_INFO,
	LEGACY_SCPI_CMD_SENSOR_VALUE,
	-1, /* SET_DEVICE_PWR_STATE */
	-1, /* GET_DEVICE_PWR_STATE */
};

struct scpi_xfer {
	u32 slot; /* has to be first element */
	u32 cmd;
	u32 status;
	const void *tx_buf;
	void *rx_buf;
	unsigned int tx_len;
	unsigned int rx_len;
	struct list_head node;
	struct completion done;
};

struct scpi_chan {
	struct mbox_client cl;
	struct mbox_chan *chan;
	void __iomem *tx_payload;
	void __iomem *rx_payload;
	struct list_head rx_pending;
	struct list_head xfers_list;
	struct scpi_xfer *xfers;
	spinlock_t rx_lock; /* locking for the rx pending list */
	struct mutex xfers_lock;
	u8 token;
};

struct scpi_drvinfo {
	u32 protocol_version;
	u32 firmware_version;
	bool is_legacy;
	int num_chans;
	int *commands;
	DECLARE_BITMAP(cmd_priority, LEGACY_SCPI_CMD_COUNT);
	atomic_t next_chan;
	struct scpi_ops *scpi_ops;
	struct scpi_chan *channels;
	struct scpi_dvfs_info *dvfs[MAX_DVFS_DOMAINS];
};

/*
 * The SCP firmware only executes in little-endian mode, so any buffers
 * shared through SCPI should have their contents converted to little-endian
 */
struct scpi_shared_mem {
	__le32 command;
	__le32 status;
	u8 payload[];
} __packed;

struct legacy_scpi_shared_mem {
	__le32 status;
	u8 payload[];
} __packed;

struct scp_capabilities {
	__le32 protocol_version;
	__le32 event_version;
	__le32 platform_version;
	__le32 commands[4];
} __packed;

struct clk_get_info {
	__le16 id;
	__le16 flags;
	__le32 min_rate;
	__le32 max_rate;
	u8 name[20];
} __packed;

struct clk_set_value {
	__le16 id;
	__le16 reserved;
	__le32 rate;
} __packed;

struct legacy_clk_set_value {
	__le32 rate;
	__le16 id;
	__le16 reserved;
} __packed;

struct dvfs_info {
	u8 domain;
	u8 opp_count;
	__le16 latency;
	struct {
		__le32 freq;
		__le32 m_volt;
	} opps[MAX_DVFS_OPPS];
} __packed;

struct dvfs_set {
	u8 domain;
	u8 index;
} __packed;

struct _scpi_sensor_info {
	__le16 sensor_id;
	u8 class;
	u8 trigger_type;
	char name[20];
};

struct dev_pstate_set {
	__le16 dev_id;
	u8 pstate;
} __packed;

static struct scpi_drvinfo *scpi_info;

static int scpi_linux_errmap[SCPI_ERR_MAX] = {
	/* better than switch case as long as return value is continuous */
	0, /* SCPI_SUCCESS */
	-EINVAL, /* SCPI_ERR_PARAM */
	-ENOEXEC, /* SCPI_ERR_ALIGN */
	-EMSGSIZE, /* SCPI_ERR_SIZE */
	-EINVAL, /* SCPI_ERR_HANDLER */
	-EACCES, /* SCPI_ERR_ACCESS */
	-ERANGE, /* SCPI_ERR_RANGE */
	-ETIMEDOUT, /* SCPI_ERR_TIMEOUT */
	-ENOMEM, /* SCPI_ERR_NOMEM */
	-EINVAL, /* SCPI_ERR_PWRSTATE */
	-EOPNOTSUPP, /* SCPI_ERR_SUPPORT */
	-EIO, /* SCPI_ERR_DEVICE */
	-EBUSY, /* SCPI_ERR_BUSY */
};

static inline int scpi_to_linux_errno(int errno)
{
	if (errno >= SCPI_SUCCESS && errno < SCPI_ERR_MAX)
		return scpi_linux_errmap[errno];
	return -EIO;
}

static void scpi_process_cmd(struct scpi_chan *ch, u32 cmd)
{
	unsigned long flags;
	struct scpi_xfer *t, *match = NULL;

	spin_lock_irqsave(&ch->rx_lock, flags);
	if (list_empty(&ch->rx_pending)) {
		spin_unlock_irqrestore(&ch->rx_lock, flags);
		return;
	}

	/* Command type is not replied by the SCP Firmware in legacy Mode
	 * We should consider that command is the head of pending RX commands
	 * if the list is not empty. In TX only mode, the list would be empty.
	 */
	if (scpi_info->is_legacy) {
		match = list_first_entry(&ch->rx_pending, struct scpi_xfer,
					 node);
		list_del(&match->node);
	} else {
		list_for_each_entry(t, &ch->rx_pending, node)
			if (CMD_XTRACT_UNIQ(t->cmd) == CMD_XTRACT_UNIQ(cmd)) {
				list_del(&t->node);
				match = t;
				break;
			}
	}
	/* check if wait_for_completion is in progress or timed-out */
	if (match && !completion_done(&match->done)) {
		unsigned int len;

		if (scpi_info->is_legacy) {
			struct legacy_scpi_shared_mem __iomem *mem =
							ch->rx_payload;

			/* RX Length is not replied by the legacy Firmware */
			len = match->rx_len;

			match->status = ioread32(&mem->status);
			memcpy_fromio(match->rx_buf, mem->payload, len);
		} else {
			struct scpi_shared_mem __iomem *mem = ch->rx_payload;

			len = min_t(unsigned int, match->rx_len, CMD_SIZE(cmd));

			match->status = ioread32(&mem->status);
			memcpy_fromio(match->rx_buf, mem->payload, len);
		}

		if (match->rx_len > len)
			memset(match->rx_buf + len, 0, match->rx_len - len);
		complete(&match->done);
	}
	spin_unlock_irqrestore(&ch->rx_lock, flags);
}

static void scpi_handle_remote_msg(struct mbox_client *c, void *msg)
{
	struct scpi_chan *ch = container_of(c, struct scpi_chan, cl);
	struct scpi_shared_mem __iomem *mem = ch->rx_payload;
	u32 cmd = 0;

	if (!scpi_info->is_legacy)
		cmd = ioread32(&mem->command);

	scpi_process_cmd(ch, cmd);
}

static void scpi_tx_prepare(struct mbox_client *c, void *msg)
{
	unsigned long flags;
	struct scpi_xfer *t = msg;
	struct scpi_chan *ch = container_of(c, struct scpi_chan, cl);
	struct scpi_shared_mem __iomem *mem = ch->tx_payload;

	if (t->tx_buf) {
		if (scpi_info->is_legacy)
			memcpy_toio(ch->tx_payload, t->tx_buf, t->tx_len);
		else
			memcpy_toio(mem->payload, t->tx_buf, t->tx_len);
	}

	if (t->rx_buf) {
		if (!(++ch->token))
			++ch->token;
		t->cmd |= FIELD_PREP(CMD_TOKEN_ID_MASK, ch->token);
		spin_lock_irqsave(&ch->rx_lock, flags);
		list_add_tail(&t->node, &ch->rx_pending);
		spin_unlock_irqrestore(&ch->rx_lock, flags);
	}

	if (!scpi_info->is_legacy)
		iowrite32(t->cmd, &mem->command);
}

static struct scpi_xfer *get_scpi_xfer(struct scpi_chan *ch)
{
	struct scpi_xfer *t;

	mutex_lock(&ch->xfers_lock);
	if (list_empty(&ch->xfers_list)) {
		mutex_unlock(&ch->xfers_lock);
		return NULL;
	}
	t = list_first_entry(&ch->xfers_list, struct scpi_xfer, node);
	list_del(&t->node);
	mutex_unlock(&ch->xfers_lock);
	return t;
}

static void put_scpi_xfer(struct scpi_xfer *t, struct scpi_chan *ch)
{
	mutex_lock(&ch->xfers_lock);
	list_add_tail(&t->node, &ch->xfers_list);
	mutex_unlock(&ch->xfers_lock);
}

static int scpi_send_message(u8 idx, void *tx_buf, unsigned int tx_len,
			     void *rx_buf, unsigned int rx_len)
{
	int ret;
	u8 chan;
	u8 cmd;
	struct scpi_xfer *msg;
	struct scpi_chan *scpi_chan;

	if (scpi_info->commands[idx] < 0)
		return -EOPNOTSUPP;

	cmd = scpi_info->commands[idx];

	if (scpi_info->is_legacy)
		chan = test_bit(cmd, scpi_info->cmd_priority) ? 1 : 0;
	else
		chan = atomic_inc_return(&scpi_info->next_chan) %
			scpi_info->num_chans;
	scpi_chan = scpi_info->channels + chan;

	msg = get_scpi_xfer(scpi_chan);
	if (!msg)
		return -ENOMEM;

	if (scpi_info->is_legacy) {
		msg->cmd = PACK_LEGACY_SCPI_CMD(cmd, tx_len);
		msg->slot = msg->cmd;
	} else {
		msg->slot = BIT(SCPI_SLOT);
		msg->cmd = PACK_SCPI_CMD(cmd, tx_len);
	}
	msg->tx_buf = tx_buf;
	msg->tx_len = tx_len;
	msg->rx_buf = rx_buf;
	msg->rx_len = rx_len;
	reinit_completion(&msg->done);

	ret = mbox_send_message(scpi_chan->chan, msg);
	if (ret < 0 || !rx_buf)
		goto out;

	if (!wait_for_completion_timeout(&msg->done, MAX_RX_TIMEOUT))
		ret = -ETIMEDOUT;
	else
		/* first status word */
		ret = msg->status;
out:
	if (ret < 0 && rx_buf) /* remove entry from the list if timed-out */
		scpi_process_cmd(scpi_chan, msg->cmd);

	put_scpi_xfer(msg, scpi_chan);
	/* SCPI error codes > 0, translate them to Linux scale*/
	return ret > 0 ? scpi_to_linux_errno(ret) : ret;
}

static u32 scpi_get_version(void)
{
	return scpi_info->protocol_version;
}

static int
scpi_clk_get_range(u16 clk_id, unsigned long *min, unsigned long *max)
{
	int ret;
	struct clk_get_info clk;
	__le16 le_clk_id = cpu_to_le16(clk_id);

	ret = scpi_send_message(CMD_GET_CLOCK_INFO, &le_clk_id,
				sizeof(le_clk_id), &clk, sizeof(clk));
	if (!ret) {
		*min = le32_to_cpu(clk.min_rate);
		*max = le32_to_cpu(clk.max_rate);
	}
	return ret;
}

static unsigned long scpi_clk_get_val(u16 clk_id)
{
	int ret;
	__le32 rate;
	__le16 le_clk_id = cpu_to_le16(clk_id);

	ret = scpi_send_message(CMD_GET_CLOCK_VALUE, &le_clk_id,
				sizeof(le_clk_id), &rate, sizeof(rate));

	return ret ? ret : le32_to_cpu(rate);
}

static int scpi_clk_set_val(u16 clk_id, unsigned long rate)
{
	int stat;
	struct clk_set_value clk = {
		.id = cpu_to_le16(clk_id),
		.rate = cpu_to_le32(rate)
	};

	return scpi_send_message(CMD_SET_CLOCK_VALUE, &clk, sizeof(clk),
				 &stat, sizeof(stat));
}

static int legacy_scpi_clk_set_val(u16 clk_id, unsigned long rate)
{
	int stat;
	struct legacy_clk_set_value clk = {
		.id = cpu_to_le16(clk_id),
		.rate = cpu_to_le32(rate)
	};

	return scpi_send_message(CMD_SET_CLOCK_VALUE, &clk, sizeof(clk),
				 &stat, sizeof(stat));
}

static int scpi_dvfs_get_idx(u8 domain)
{
	int ret;
	u8 dvfs_idx;

	ret = scpi_send_message(CMD_GET_DVFS, &domain, sizeof(domain),
				&dvfs_idx, sizeof(dvfs_idx));

	return ret ? ret : dvfs_idx;
}

static int scpi_dvfs_set_idx(u8 domain, u8 index)
{
	int stat;
	struct dvfs_set dvfs = {domain, index};

	return scpi_send_message(CMD_SET_DVFS, &dvfs, sizeof(dvfs),
				 &stat, sizeof(stat));
}

static int opp_cmp_func(const void *opp1, const void *opp2)
{
	const struct scpi_opp *t1 = opp1, *t2 = opp2;

	return t1->freq - t2->freq;
}

static struct scpi_dvfs_info *scpi_dvfs_get_info(u8 domain)
{
	struct scpi_dvfs_info *info;
	struct scpi_opp *opp;
	struct dvfs_info buf;
	int ret, i;

	if (domain >= MAX_DVFS_DOMAINS)
		return ERR_PTR(-EINVAL);

	if (scpi_info->dvfs[domain])	/* data already populated */
		return scpi_info->dvfs[domain];

	ret = scpi_send_message(CMD_GET_DVFS_INFO, &domain, sizeof(domain),
				&buf, sizeof(buf));
	if (ret)
		return ERR_PTR(ret);

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	info->count = buf.opp_count;
	info->latency = le16_to_cpu(buf.latency) * 1000; /* uS to nS */

	info->opps = kcalloc(info->count, sizeof(*opp), GFP_KERNEL);
	if (!info->opps) {
		kfree(info);
		return ERR_PTR(-ENOMEM);
	}

	for (i = 0, opp = info->opps; i < info->count; i++, opp++) {
		opp->freq = le32_to_cpu(buf.opps[i].freq);
		opp->m_volt = le32_to_cpu(buf.opps[i].m_volt);
	}

	sort(info->opps, info->count, sizeof(*opp), opp_cmp_func, NULL);

	scpi_info->dvfs[domain] = info;
	return info;
}

static int scpi_dev_domain_id(struct device *dev)
{
	struct of_phandle_args clkspec;

	if (of_parse_phandle_with_args(dev->of_node, "clocks", "#clock-cells",
				       0, &clkspec))
		return -EINVAL;

	return clkspec.args[0];
}

static struct scpi_dvfs_info *scpi_dvfs_info(struct device *dev)
{
	int domain = scpi_dev_domain_id(dev);

	if (domain < 0)
		return ERR_PTR(domain);

	return scpi_dvfs_get_info(domain);
}

static int scpi_dvfs_get_transition_latency(struct device *dev)
{
	struct scpi_dvfs_info *info = scpi_dvfs_info(dev);

	if (IS_ERR(info))
		return PTR_ERR(info);

	return info->latency;
}

static int scpi_dvfs_add_opps_to_device(struct device *dev)
{
	int idx, ret;
	struct scpi_opp *opp;
	struct scpi_dvfs_info *info = scpi_dvfs_info(dev);

	if (IS_ERR(info))
		return PTR_ERR(info);

	if (!info->opps)
		return -EIO;

	for (opp = info->opps, idx = 0; idx < info->count; idx++, opp++) {
		ret = dev_pm_opp_add(dev, opp->freq, opp->m_volt * 1000);
		if (ret) {
			dev_warn(dev, "failed to add opp %uHz %umV\n",
				 opp->freq, opp->m_volt);
			while (idx-- > 0)
				dev_pm_opp_remove(dev, (--opp)->freq);
			return ret;
		}
	}
	return 0;
}

static int scpi_sensor_get_capability(u16 *sensors)
{
	__le16 cap;
	int ret;

	ret = scpi_send_message(CMD_SENSOR_CAPABILITIES, NULL, 0, &cap,
				sizeof(cap));
	if (!ret)
		*sensors = le16_to_cpu(cap);

	return ret;
}

static int scpi_sensor_get_info(u16 sensor_id, struct scpi_sensor_info *info)
{
	__le16 id = cpu_to_le16(sensor_id);
	struct _scpi_sensor_info _info;
	int ret;

	ret = scpi_send_message(CMD_SENSOR_INFO, &id, sizeof(id),
				&_info, sizeof(_info));
	if (!ret) {
		memcpy(info, &_info, sizeof(*info));
		info->sensor_id = le16_to_cpu(_info.sensor_id);
	}

	return ret;
}

static int scpi_sensor_get_value(u16 sensor, u64 *val)
{
	__le16 id = cpu_to_le16(sensor);
	__le64 value;
	int ret;

	ret = scpi_send_message(CMD_SENSOR_VALUE, &id, sizeof(id),
				&value, sizeof(value));
	if (ret)
		return ret;

	if (scpi_info->is_legacy)
		/* only 32-bits supported, upper 32 bits can be junk */
		*val = le32_to_cpup((__le32 *)&value);
	else
		*val = le64_to_cpu(value);

	return 0;
}

static int scpi_device_get_power_state(u16 dev_id)
{
	int ret;
	u8 pstate;
	__le16 id = cpu_to_le16(dev_id);

	ret = scpi_send_message(CMD_GET_DEVICE_PWR_STATE, &id,
				sizeof(id), &pstate, sizeof(pstate));
	return ret ? ret : pstate;
}

static int scpi_device_set_power_state(u16 dev_id, u8 pstate)
{
	int stat;
	struct dev_pstate_set dev_set = {
		.dev_id = cpu_to_le16(dev_id),
		.pstate = pstate,
	};

	return scpi_send_message(CMD_SET_DEVICE_PWR_STATE, &dev_set,
				 sizeof(dev_set), &stat, sizeof(stat));
}

static struct scpi_ops scpi_ops = {
	.get_version = scpi_get_version,
	.clk_get_range = scpi_clk_get_range,
	.clk_get_val = scpi_clk_get_val,
	.clk_set_val = scpi_clk_set_val,
	.dvfs_get_idx = scpi_dvfs_get_idx,
	.dvfs_set_idx = scpi_dvfs_set_idx,
	.dvfs_get_info = scpi_dvfs_get_info,
	.device_domain_id = scpi_dev_domain_id,
	.get_transition_latency = scpi_dvfs_get_transition_latency,
	.add_opps_to_device = scpi_dvfs_add_opps_to_device,
	.sensor_get_capability = scpi_sensor_get_capability,
	.sensor_get_info = scpi_sensor_get_info,
	.sensor_get_value = scpi_sensor_get_value,
	.device_get_power_state = scpi_device_get_power_state,
	.device_set_power_state = scpi_device_set_power_state,
};

struct scpi_ops *get_scpi_ops(void)
{
	return scpi_info ? scpi_info->scpi_ops : NULL;
}
EXPORT_SYMBOL_GPL(get_scpi_ops);

static int scpi_init_versions(struct scpi_drvinfo *info)
{
	int ret;
	struct scp_capabilities caps;

	ret = scpi_send_message(CMD_SCPI_CAPABILITIES, NULL, 0,
				&caps, sizeof(caps));
	if (!ret) {
		info->protocol_version = le32_to_cpu(caps.protocol_version);
		info->firmware_version = le32_to_cpu(caps.platform_version);
	}
	/* Ignore error if not implemented */
	if (scpi_info->is_legacy && ret == -EOPNOTSUPP)
		return 0;

	return ret;
}

static ssize_t protocol_version_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct scpi_drvinfo *scpi_info = dev_get_drvdata(dev);

	return sprintf(buf, "%lu.%lu\n",
		FIELD_GET(PROTO_REV_MAJOR_MASK, scpi_info->protocol_version),
		FIELD_GET(PROTO_REV_MINOR_MASK, scpi_info->protocol_version));
}
static DEVICE_ATTR_RO(protocol_version);

static ssize_t firmware_version_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct scpi_drvinfo *scpi_info = dev_get_drvdata(dev);

	return sprintf(buf, "%lu.%lu.%lu\n",
		FIELD_GET(FW_REV_MAJOR_MASK, scpi_info->firmware_version),
		FIELD_GET(FW_REV_MINOR_MASK, scpi_info->firmware_version),
		FIELD_GET(FW_REV_PATCH_MASK, scpi_info->firmware_version));
}
static DEVICE_ATTR_RO(firmware_version);

static struct attribute *versions_attrs[] = {
	&dev_attr_firmware_version.attr,
	&dev_attr_protocol_version.attr,
	NULL,
};
ATTRIBUTE_GROUPS(versions);

static void scpi_free_channels(void *data)
{
	struct scpi_drvinfo *info = data;
	int i;

	for (i = 0; i < info->num_chans; i++)
		mbox_free_channel(info->channels[i].chan);
}

static int scpi_remove(struct platform_device *pdev)
{
	int i;
	struct scpi_drvinfo *info = platform_get_drvdata(pdev);

	scpi_info = NULL; /* stop exporting SCPI ops through get_scpi_ops */

	for (i = 0; i < MAX_DVFS_DOMAINS && info->dvfs[i]; i++) {
		kfree(info->dvfs[i]->opps);
		kfree(info->dvfs[i]);
	}

	return 0;
}

#define MAX_SCPI_XFERS		10
static int scpi_alloc_xfer_list(struct device *dev, struct scpi_chan *ch)
{
	int i;
	struct scpi_xfer *xfers;

	xfers = devm_kcalloc(dev, MAX_SCPI_XFERS, sizeof(*xfers), GFP_KERNEL);
	if (!xfers)
		return -ENOMEM;

	ch->xfers = xfers;
	for (i = 0; i < MAX_SCPI_XFERS; i++, xfers++) {
		init_completion(&xfers->done);
		list_add_tail(&xfers->node, &ch->xfers_list);
	}

	return 0;
}

static const struct of_device_id legacy_scpi_of_match[] = {
	{.compatible = "arm,scpi-pre-1.0"},
	{},
};

static int scpi_probe(struct platform_device *pdev)
{
	int count, idx, ret;
	struct resource res;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	scpi_info = devm_kzalloc(dev, sizeof(*scpi_info), GFP_KERNEL);
	if (!scpi_info)
		return -ENOMEM;

	if (of_match_device(legacy_scpi_of_match, &pdev->dev))
		scpi_info->is_legacy = true;

	count = of_count_phandle_with_args(np, "mboxes", "#mbox-cells");
	if (count < 0) {
		dev_err(dev, "no mboxes property in '%pOF'\n", np);
		return -ENODEV;
	}

	scpi_info->channels = devm_kcalloc(dev, count, sizeof(struct scpi_chan),
					   GFP_KERNEL);
	if (!scpi_info->channels)
		return -ENOMEM;

	ret = devm_add_action(dev, scpi_free_channels, scpi_info);
	if (ret)
		return ret;

	for (; scpi_info->num_chans < count; scpi_info->num_chans++) {
		resource_size_t size;
		int idx = scpi_info->num_chans;
		struct scpi_chan *pchan = scpi_info->channels + idx;
		struct mbox_client *cl = &pchan->cl;
		struct device_node *shmem = of_parse_phandle(np, "shmem", idx);

		ret = of_address_to_resource(shmem, 0, &res);
		of_node_put(shmem);
		if (ret) {
			dev_err(dev, "failed to get SCPI payload mem resource\n");
			return ret;
		}

		size = resource_size(&res);
		pchan->rx_payload = devm_ioremap(dev, res.start, size);
		if (!pchan->rx_payload) {
			dev_err(dev, "failed to ioremap SCPI payload\n");
			return -EADDRNOTAVAIL;
		}
		pchan->tx_payload = pchan->rx_payload + (size >> 1);

		cl->dev = dev;
		cl->rx_callback = scpi_handle_remote_msg;
		cl->tx_prepare = scpi_tx_prepare;
		cl->tx_block = true;
		cl->tx_tout = 20;
		cl->knows_txdone = false; /* controller can't ack */

		INIT_LIST_HEAD(&pchan->rx_pending);
		INIT_LIST_HEAD(&pchan->xfers_list);
		spin_lock_init(&pchan->rx_lock);
		mutex_init(&pchan->xfers_lock);

		ret = scpi_alloc_xfer_list(dev, pchan);
		if (!ret) {
			pchan->chan = mbox_request_channel(cl, idx);
			if (!IS_ERR(pchan->chan))
				continue;
			ret = PTR_ERR(pchan->chan);
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "failed to get channel%d err %d\n",
					idx, ret);
		}
		return ret;
	}

	scpi_info->commands = scpi_std_commands;

	platform_set_drvdata(pdev, scpi_info);

	if (scpi_info->is_legacy) {
		/* Replace with legacy variants */
		scpi_ops.clk_set_val = legacy_scpi_clk_set_val;
		scpi_info->commands = scpi_legacy_commands;

		/* Fill priority bitmap */
		for (idx = 0; idx < ARRAY_SIZE(legacy_hpriority_cmds); idx++)
			set_bit(legacy_hpriority_cmds[idx],
				scpi_info->cmd_priority);
	}

	ret = scpi_init_versions(scpi_info);
	if (ret) {
		dev_err(dev, "incorrect or no SCP firmware found\n");
		return ret;
	}

	if (scpi_info->is_legacy && !scpi_info->protocol_version &&
	    !scpi_info->firmware_version)
		dev_info(dev, "SCP Protocol legacy pre-1.0 firmware\n");
	else
		dev_info(dev, "SCP Protocol %lu.%lu Firmware %lu.%lu.%lu version\n",
			 FIELD_GET(PROTO_REV_MAJOR_MASK,
				   scpi_info->protocol_version),
			 FIELD_GET(PROTO_REV_MINOR_MASK,
				   scpi_info->protocol_version),
			 FIELD_GET(FW_REV_MAJOR_MASK,
				   scpi_info->firmware_version),
			 FIELD_GET(FW_REV_MINOR_MASK,
				   scpi_info->firmware_version),
			 FIELD_GET(FW_REV_PATCH_MASK,
				   scpi_info->firmware_version));
	scpi_info->scpi_ops = &scpi_ops;

	return devm_of_platform_populate(dev);
}

static const struct of_device_id scpi_of_match[] = {
	{.compatible = "arm,scpi"},
	{.compatible = "arm,scpi-pre-1.0"},
	{},
};

MODULE_DEVICE_TABLE(of, scpi_of_match);

static struct platform_driver scpi_driver = {
	.driver = {
		.name = "scpi_protocol",
		.of_match_table = scpi_of_match,
		.dev_groups = versions_groups,
	},
	.probe = scpi_probe,
	.remove = scpi_remove,
};
module_platform_driver(scpi_driver);

MODULE_AUTHOR("Sudeep Holla <sudeep.holla@arm.com>");
MODULE_DESCRIPTION("ARM SCPI mailbox protocol driver");
MODULE_LICENSE("GPL v2");

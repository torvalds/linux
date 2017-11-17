/*
 * System Control and Power Interface (SCPI) Message Protocol driver
 *
 * Copyright (C) 2014 ARM Ltd.
 * Copyright (c) 2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/mailbox_client.h>
#include <linux/slab.h>
#include <soc/rockchip/scpi.h>
#include <soc/rockchip/rk3368-mailbox.h>

#define SCPI_VERSION		0x01000002	/* version: 1.0.0.2 */

#define CMD_ID_SHIFT		0
#define CMD_ID_MASK		0xff
#define CMD_SENDER_ID_SHIFT	8
#define CMD_SENDER_ID_MASK	0xff
#define CMD_DATA_SIZE_SHIFT	20
#define CMD_DATA_SIZE_MASK	0x1ff
#define PACK_SCPI_CMD(cmd, sender, txsz)				\
	((((cmd) & CMD_ID_MASK) << CMD_ID_SHIFT) |			\
	(((sender) & CMD_SENDER_ID_MASK) << CMD_SENDER_ID_SHIFT) |	\
	(((txsz) & CMD_DATA_SIZE_MASK) << CMD_DATA_SIZE_SHIFT))
#define SCPI_CMD_DEFAULT_TIMEOUT_MS  1000

#define MAX_DVFS_DOMAINS	3
#define MAX_DVFS_OPPS		8
#define DVFS_LATENCY(hdr)	((hdr) >> 16)
#define DVFS_OPP_COUNT(hdr)	(((hdr) >> 8) & 0xff)

static int max_chan_num = 0;
static DECLARE_BITMAP(bm_mbox_chans, 4);
static DEFINE_MUTEX(scpi_mtx);

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
	SCPI_ERR_MAX
};

enum scpi_client_id {
	SCPI_CL_NONE,
	SCPI_CL_CLOCKS,
	SCPI_CL_DVFS,
	SCPI_CL_POWER,
	SCPI_CL_THERMAL,
	SCPI_CL_DDR,
	SCPI_CL_SYS,
	SCPI_MAX,
};

enum scpi_ddr_cmd {
	SCPI_DDR_INIT,
	SCPI_DDR_SET_FREQ,
	SCPI_DDR_ROUND_RATE,
	SCPI_DDR_AUTO_SELF_REFRESH,
	SCPI_DDR_BANDWIDTH_GET,
	SCPI_DDR_GET_FREQ,
	SCPI_DDR_SEND_TIMING,
	SCPI_DDR_DCLK_MODE,
};

enum scpi_sys_cmd {
	SCPI_SYS_GET_VERSION,
	SCPI_SYS_REFRESH_MCU_FREQ,
	SCPI_SYS_SET_MCU_STATE_SUSPEND,
	SCPI_SYS_SET_MCU_STATE_RESUME,
	SCPI_SYS_SET_JTAGMUX_ON_OFF,
};

enum scpi_std_cmd {
	SCPI_CMD_INVALID		= 0x00,
	SCPI_CMD_SCPI_READY		= 0x01,
	SCPI_CMD_SCPI_CAPABILITIES	= 0x02,
	SCPI_CMD_EVENT			= 0x03,
	SCPI_CMD_SET_CSS_PWR_STATE	= 0x04,
	SCPI_CMD_GET_CSS_PWR_STATE	= 0x05,
	SCPI_CMD_CFG_PWR_STATE_STAT	= 0x06,
	SCPI_CMD_GET_PWR_STATE_STAT	= 0x07,
	SCPI_CMD_SYS_PWR_STATE		= 0x08,
	SCPI_CMD_L2_READY		= 0x09,
	SCPI_CMD_SET_AP_TIMER		= 0x0a,
	SCPI_CMD_CANCEL_AP_TIME		= 0x0b,
	SCPI_CMD_DVFS_CAPABILITIES	= 0x0c,
	SCPI_CMD_GET_DVFS_INFO		= 0x0d,
	SCPI_CMD_SET_DVFS		= 0x0e,
	SCPI_CMD_GET_DVFS		= 0x0f,
	SCPI_CMD_GET_DVFS_STAT		= 0x10,
	SCPI_CMD_SET_RTC		= 0x11,
	SCPI_CMD_GET_RTC		= 0x12,
	SCPI_CMD_CLOCK_CAPABILITIES	= 0x13,
	SCPI_CMD_SET_CLOCK_INDEX	= 0x14,
	SCPI_CMD_SET_CLOCK_VALUE	= 0x15,
	SCPI_CMD_GET_CLOCK_VALUE	= 0x16,
	SCPI_CMD_PSU_CAPABILITIES	= 0x17,
	SCPI_CMD_SET_PSU		= 0x18,
	SCPI_CMD_GET_PSU		= 0x19,
	SCPI_CMD_SENSOR_CAPABILITIES	= 0x1a,
	SCPI_CMD_SENSOR_INFO		= 0x1b,
	SCPI_CMD_SENSOR_VALUE		= 0x1c,
	SCPI_CMD_SENSOR_CFG_PERIODIC	= 0x1d,
	SCPI_CMD_SENSOR_CFG_BOUNDS	= 0x1e,
	SCPI_CMD_SENSOR_ASYNC_VALUE	= 0x1f,
	SCPI_CMD_COUNT
};

enum scpi_thermal_cmd {
	SCPI_THERMAL_GET_TSADC_DATA,
	SCPI_THERMAL_SET_TSADC_CYCLE,
	SCPI_THERMAL_COUNT
};

static int high_priority_cmds[] = {
	SCPI_CMD_GET_CSS_PWR_STATE,
	SCPI_CMD_CFG_PWR_STATE_STAT,
	SCPI_CMD_GET_PWR_STATE_STAT,
	SCPI_CMD_SET_DVFS,
	SCPI_CMD_GET_DVFS,
	SCPI_CMD_SET_RTC,
	SCPI_CMD_GET_RTC,
	SCPI_CMD_SET_CLOCK_INDEX,
	SCPI_CMD_SET_CLOCK_VALUE,
	SCPI_CMD_GET_CLOCK_VALUE,
	SCPI_CMD_SET_PSU,
	SCPI_CMD_GET_PSU,
	SCPI_CMD_SENSOR_CFG_PERIODIC,
	SCPI_CMD_SENSOR_CFG_BOUNDS,
};

struct scpi_data_buf {
	int client_id;
	struct rk3368_mbox_msg *data;
	struct completion complete;
	int timeout_ms;
};

struct scpi_mcu_ver {
	u32  scpi_ver;
	char mcu_ver[16];
};

static struct scpi_opp *scpi_opps[MAX_DVFS_DOMAINS];

static struct device *the_scpi_device;

static int scpi_linux_errmap[SCPI_ERR_MAX] = {
	0, -EINVAL, -ENOEXEC, -EMSGSIZE,
	-EINVAL, -EACCES, -ERANGE, -ETIMEDOUT,
	-ENOMEM, -EINVAL, -EOPNOTSUPP, -EIO,
};

static inline int scpi_to_linux_errno(int errno)
{
	if (errno >= SCPI_SUCCESS && errno < SCPI_ERR_MAX)
		return scpi_linux_errmap[errno];
	return -EIO;
}

static bool __maybe_unused high_priority_chan_supported(int cmd)
{
	int idx;

	for (idx = 0; idx < ARRAY_SIZE(high_priority_cmds); idx++)
		if (cmd == high_priority_cmds[idx])
			return true;
	return false;
}

static int scpi_alloc_mbox_chan(void)
{
	int index;

	mutex_lock(&scpi_mtx);

	index = find_first_zero_bit(bm_mbox_chans, max_chan_num);
	if (index >= max_chan_num) {
		pr_err("alloc mailbox channel failed\n");
		mutex_unlock(&scpi_mtx);
		return -EBUSY;
	}

	set_bit(index, bm_mbox_chans);

	mutex_unlock(&scpi_mtx);
	return index;
}

static void scpi_free_mbox_chan(int chan)
{
	int index = chan;

	mutex_lock(&scpi_mtx);

	if (index < max_chan_num && index >= 0)
		clear_bit(index, bm_mbox_chans);

	mutex_unlock(&scpi_mtx);
}

static void scpi_rx_callback(struct mbox_client *cl, void *msg)
{
	struct rk3368_mbox_msg *data = (struct rk3368_mbox_msg *)msg;
	struct scpi_data_buf *scpi_buf = data->cl_data;

	complete(&scpi_buf->complete);
}

static int send_scpi_cmd(struct scpi_data_buf *scpi_buf, int index)
{
	struct mbox_chan *chan;
	struct mbox_client cl;
	struct rk3368_mbox_msg *data = scpi_buf->data;
	u32 status;
	int ret;
	int timeout = msecs_to_jiffies(scpi_buf->timeout_ms);

	if (!the_scpi_device) {
		pr_err("Scpi initializes unsuccessfully\n");
		return -EIO;
	}

	cl.dev = the_scpi_device;
	cl.rx_callback = scpi_rx_callback;
	cl.tx_done = NULL;
	cl.tx_prepare = NULL;
	cl.tx_block = false;
	cl.knows_txdone = false;

	chan = mbox_request_channel(&cl, index);
	if (IS_ERR(chan)) {
		scpi_free_mbox_chan(index);
		return PTR_ERR(chan);
	}

	init_completion(&scpi_buf->complete);
	if (mbox_send_message(chan, (void *)data) < 0) {
		status = SCPI_ERR_TIMEOUT;
		goto free_channel;
	}

	ret = wait_for_completion_timeout(&scpi_buf->complete, timeout);
	if (ret == 0) {
		status = SCPI_ERR_TIMEOUT;
		goto free_channel;
	}
	status = *(u32 *)(data->rx_buf); /* read first word */

free_channel:
	mbox_free_channel(chan);
	scpi_free_mbox_chan(index);

	return scpi_to_linux_errno(status);
}

#define SCPI_SETUP_DBUF(scpi_buf, mbox_buf, _client_id,\
			_cmd, _tx_buf, _rx_buf) \
do {						\
	struct rk3368_mbox_msg *pdata = &mbox_buf;	\
	pdata->cmd = _cmd;			\
	pdata->tx_buf = &_tx_buf;		\
	pdata->tx_size = sizeof(_tx_buf);	\
	pdata->rx_buf = &_rx_buf;		\
	pdata->rx_size = sizeof(_rx_buf);	\
	scpi_buf.client_id = _client_id;	\
	scpi_buf.data = pdata;			\
	scpi_buf.timeout_ms = SCPI_CMD_DEFAULT_TIMEOUT_MS; \
} while (0)

#define SCPI_SETUP_DBUF_BY_SIZE(scpi_buf, mbox_buf, _client_id,		\
				_cmd, _tx_buf, _tx_size, _rx_buf)	\
do {									\
	struct rk3368_mbox_msg *pdata = &mbox_buf;			\
	pdata->cmd = _cmd;						\
	pdata->tx_buf = _tx_buf;					\
	pdata->tx_size = _tx_size;					\
	pdata->rx_buf = &_rx_buf;					\
	pdata->rx_size = sizeof(_rx_buf);				\
	scpi_buf.client_id = _client_id;				\
	scpi_buf.data = pdata;						\
	scpi_buf.timeout_ms = SCPI_CMD_DEFAULT_TIMEOUT_MS;		\
} while (0)

static int scpi_execute_cmd(struct scpi_data_buf *scpi_buf)
{
	struct rk3368_mbox_msg *data;
	int index;

	if (!scpi_buf || !scpi_buf->data)
		return -EINVAL;

	index = scpi_alloc_mbox_chan();
	if (index < 0)
		return -EBUSY;

	data = scpi_buf->data;
	data->cmd = PACK_SCPI_CMD(data->cmd, scpi_buf->client_id,
				  data->tx_size);
	data->cl_data = scpi_buf;

	return send_scpi_cmd(scpi_buf, index);
}

unsigned long scpi_clk_get_val(u16 clk_id)
{
	struct scpi_data_buf sdata;
	struct rk3368_mbox_msg mdata;
	struct __packed {
		u32 status;
		u32 clk_rate;
	} buf;

	SCPI_SETUP_DBUF(sdata, mdata, SCPI_CL_CLOCKS,
			SCPI_CMD_GET_CLOCK_VALUE, clk_id, buf);
	if (scpi_execute_cmd(&sdata))
		return 0;

	return buf.clk_rate;
}
EXPORT_SYMBOL_GPL(scpi_clk_get_val);

int scpi_clk_set_val(u16 clk_id, unsigned long rate)
{
	struct scpi_data_buf sdata;
	struct rk3368_mbox_msg mdata;
	int stat;
	struct __packed {
		u32 clk_rate;
		u16 clk_id;
	} buf;

	buf.clk_rate = (u32)rate;
	buf.clk_id = clk_id;

	SCPI_SETUP_DBUF(sdata, mdata, SCPI_CL_CLOCKS,
			SCPI_CMD_SET_CLOCK_VALUE, buf, stat);
	return scpi_execute_cmd(&sdata);
}
EXPORT_SYMBOL_GPL(scpi_clk_set_val);

struct scpi_opp *scpi_dvfs_get_opps(u8 domain)
{
	struct scpi_data_buf sdata;
	struct rk3368_mbox_msg mdata;
	struct __packed {
		u32 status;
		u32 header;
		struct scpi_opp_entry opp[MAX_DVFS_OPPS];
	} buf;
	struct scpi_opp *opps;
	size_t opps_sz;
	int count, ret;

	if (domain >= MAX_DVFS_DOMAINS)
		return ERR_PTR(-EINVAL);

	if (scpi_opps[domain])	/* data already populated */
		return scpi_opps[domain];

	SCPI_SETUP_DBUF(sdata, mdata, SCPI_CL_DVFS,
			SCPI_CMD_GET_DVFS_INFO, domain, buf);
	ret = scpi_execute_cmd(&sdata);
	if (ret)
		return ERR_PTR(ret);

	opps = kmalloc(sizeof(*opps), GFP_KERNEL);
	if (!opps)
		return ERR_PTR(-ENOMEM);

	count = DVFS_OPP_COUNT(buf.header);
	opps_sz = count * sizeof(*(opps->opp));

	opps->count = count;
	opps->latency = DVFS_LATENCY(buf.header);
	opps->opp = kmalloc(opps_sz, GFP_KERNEL);
	if (!opps->opp) {
		kfree(opps);
		return ERR_PTR(-ENOMEM);
	}

	memcpy(opps->opp, &buf.opp[0], opps_sz);
	scpi_opps[domain] = opps;

	return opps;
}
EXPORT_SYMBOL_GPL(scpi_dvfs_get_opps);

int scpi_dvfs_get_idx(u8 domain)
{
	struct scpi_data_buf sdata;
	struct rk3368_mbox_msg mdata;
	struct __packed {
		u32 status;
		u8 dvfs_idx;
	} buf;
	int ret;

	if (domain >= MAX_DVFS_DOMAINS)
		return -EINVAL;

	SCPI_SETUP_DBUF(sdata, mdata, SCPI_CL_DVFS,
			SCPI_CMD_GET_DVFS, domain, buf);
	ret = scpi_execute_cmd(&sdata);

	if (!ret)
		ret = buf.dvfs_idx;
	return ret;
}
EXPORT_SYMBOL_GPL(scpi_dvfs_get_idx);

int scpi_dvfs_set_idx(u8 domain, u8 idx)
{
	struct scpi_data_buf sdata;
	struct rk3368_mbox_msg mdata;
	struct __packed {
		u8 dvfs_domain;
		u8 dvfs_idx;
	} buf;
	int stat;

	buf.dvfs_idx = idx;
	buf.dvfs_domain = domain;

	if (domain >= MAX_DVFS_DOMAINS)
		return -EINVAL;

	SCPI_SETUP_DBUF(sdata, mdata, SCPI_CL_DVFS,
			SCPI_CMD_SET_DVFS, buf, stat);
	return scpi_execute_cmd(&sdata);
}
EXPORT_SYMBOL_GPL(scpi_dvfs_set_idx);

int scpi_get_sensor(char *name)
{
	struct scpi_data_buf sdata;
	struct rk3368_mbox_msg mdata;
	struct __packed {
		u32 status;
		u16 sensors;
	} cap_buf;
	struct __packed {
		u32 status;
		u16 sensor;
		u8 class;
		u8 trigger;
		char name[20];
	} info_buf;
	int ret;
	u16 sensor_id;

	/* This should be handled by a generic macro */
	do {
		struct rk3368_mbox_msg *pdata = &mdata;

		pdata->cmd = SCPI_CMD_SENSOR_CAPABILITIES;
		pdata->tx_size = 0;
		pdata->rx_buf = &cap_buf;
		pdata->rx_size = sizeof(cap_buf);
		sdata.client_id = SCPI_CL_THERMAL;
		sdata.data = pdata;
	} while (0);

	ret = scpi_execute_cmd(&sdata);
	if (ret)
		goto out;

	ret = -ENODEV;
	for (sensor_id = 0; sensor_id < cap_buf.sensors; sensor_id++) {
		SCPI_SETUP_DBUF(sdata, mdata, SCPI_CL_THERMAL,
				SCPI_CMD_SENSOR_INFO, sensor_id, info_buf);
		ret = scpi_execute_cmd(&sdata);
		if (ret)
			break;

		if (!strcmp(name, info_buf.name)) {
			ret = sensor_id;
			break;
		}
	}
out:
	return ret;
}
EXPORT_SYMBOL_GPL(scpi_get_sensor);

int scpi_get_sensor_value(u16 sensor, u32 *val)
{
	struct scpi_data_buf sdata;
	struct rk3368_mbox_msg mdata;
	struct __packed {
		u32 status;
		u32 val;
	} buf;
	int ret;

	SCPI_SETUP_DBUF(sdata, mdata, SCPI_CL_THERMAL, SCPI_CMD_SENSOR_VALUE,
			sensor, buf);

	ret = scpi_execute_cmd(&sdata);
	if (ret)
		*val = buf.val;

	return ret;
}
EXPORT_SYMBOL_GPL(scpi_get_sensor_value);

static int scpi_get_version(u32 old, struct scpi_mcu_ver *ver)
{
	int ret;
	struct scpi_data_buf sdata;
	struct rk3368_mbox_msg mdata;
	struct __packed {
		u32 status;
		struct scpi_mcu_ver version;
	} buf;

	memset(&buf, 0, sizeof(buf));
	SCPI_SETUP_DBUF(sdata, mdata, SCPI_CL_SYS, SCPI_SYS_GET_VERSION,
			old, buf);

	ret = scpi_execute_cmd(&sdata);
	if (ret) {
		pr_err("get scpi version from MCU failed, ret=%d\n", ret);
		goto OUT;
	}

	memcpy(ver, &(buf.version), sizeof(*ver));

OUT:
	return ret;
}

int scpi_sys_set_mcu_state_suspend(void)
{
	struct scpi_data_buf sdata;
	struct rk3368_mbox_msg mdata;
	struct __packed1 {
		u32 status;
	} tx_buf;
	struct __packed2 {
		u32 status;
	} rx_buf;

	tx_buf.status = 0;
	SCPI_SETUP_DBUF(sdata, mdata, SCPI_CL_SYS,
			SCPI_SYS_SET_MCU_STATE_SUSPEND, tx_buf, rx_buf);
	return scpi_execute_cmd(&sdata);
}
EXPORT_SYMBOL_GPL(scpi_sys_set_mcu_state_suspend);

int scpi_sys_set_mcu_state_resume(void)
{
	struct scpi_data_buf sdata;
	struct rk3368_mbox_msg mdata;
	struct __packed1 {
		u32 status;
	} tx_buf;
	struct __packed2 {
		u32 status;
	} rx_buf;

	tx_buf.status = 0;

	SCPI_SETUP_DBUF(sdata, mdata, SCPI_CL_SYS,
			SCPI_SYS_SET_MCU_STATE_RESUME, tx_buf, rx_buf);
	return scpi_execute_cmd(&sdata);
}
EXPORT_SYMBOL_GPL(scpi_sys_set_mcu_state_resume);

int scpi_sys_set_jtagmux_on_off(u32 en)
{
	int ret;
	struct scpi_data_buf sdata;
	struct rk3368_mbox_msg mdata;
	struct __packed1 {
		u32 enable;
	} tx_buf;

	struct __packed2 {
		u32 status;
	} rx_buf;

	tx_buf.enable = en;
	SCPI_SETUP_DBUF(sdata, mdata, SCPI_CL_SYS,
			SCPI_SYS_SET_JTAGMUX_ON_OFF, tx_buf, rx_buf);

	ret = scpi_execute_cmd(&sdata);
	if (ret)
		pr_err("set jtagmux on-off failed, ret=%d\n", ret);
	else
		ret = rx_buf.status;

	return ret;
}
EXPORT_SYMBOL_GPL(scpi_sys_set_jtagmux_on_off);

int scpi_ddr_dclk_mode(u32 dclk_mode)
{
	struct scpi_data_buf sdata;
	struct rk3368_mbox_msg mdata;
	struct __packed1 {
		u32 dclk_mode;
	} tx_buf;
	struct __packed2 {
		u32 status;
	} rx_buf;

	tx_buf.dclk_mode = (u32)dclk_mode;
	SCPI_SETUP_DBUF(sdata, mdata, SCPI_CL_DDR,
			SCPI_DDR_DCLK_MODE, tx_buf, rx_buf);
	return scpi_execute_cmd(&sdata);
}
EXPORT_SYMBOL_GPL(scpi_ddr_dclk_mode);

int scpi_ddr_init(u32 dram_speed_bin, u32 freq, u32 lcdc_type, u32 addr_mcu_el3)
{
	struct scpi_data_buf sdata;
	struct rk3368_mbox_msg mdata;
	struct __packed1 {
		u32 dram_speed_bin;
		u32 freq;
		u32 lcdc_type;
		u32 addr_mcu_el3;
	} tx_buf;
	struct __packed2 {
		u32 status;
	} rx_buf;

	tx_buf.dram_speed_bin = (u32)dram_speed_bin;
	tx_buf.freq = (u32)freq;
	tx_buf.lcdc_type = (u32)lcdc_type;
	tx_buf.addr_mcu_el3 = addr_mcu_el3;
	SCPI_SETUP_DBUF(sdata, mdata, SCPI_CL_DDR,
			SCPI_DDR_INIT, tx_buf, rx_buf);
	return scpi_execute_cmd(&sdata);
}
EXPORT_SYMBOL_GPL(scpi_ddr_init);

int scpi_ddr_set_clk_rate(u32 rate, u32 lcdc_type)
{
	int ret;

	struct scpi_data_buf sdata;
	struct rk3368_mbox_msg mdata;
	struct __packed1 {
		u32 clk_rate;
		u32 lcdc_type;
	} tx_buf;
	struct __packed2 {
		u32 status;
		u32 freq;
	} rx_buf;

	tx_buf.clk_rate = (u32)rate;
	tx_buf.lcdc_type = (u32)lcdc_type;
	SCPI_SETUP_DBUF(sdata, mdata, SCPI_CL_DDR,
			SCPI_DDR_SET_FREQ, tx_buf, rx_buf);
	ret = scpi_execute_cmd(&sdata);

	if ((!ret) && (!rx_buf.status))
		ret = rx_buf.freq;
	else
		ret = 0;

	return ret;
}
EXPORT_SYMBOL_GPL(scpi_ddr_set_clk_rate);

int scpi_ddr_send_timing(u32 *p, u32 size)
{
	struct scpi_data_buf sdata;
	struct rk3368_mbox_msg mdata;
	struct __packed2 {
		u32 status;
	} rx_buf;
	SCPI_SETUP_DBUF_BY_SIZE(sdata, mdata, SCPI_CL_DDR,
				SCPI_DDR_SEND_TIMING, p, size, rx_buf);
	return scpi_execute_cmd(&sdata);
}
EXPORT_SYMBOL_GPL(scpi_ddr_send_timing);

int scpi_ddr_round_rate(u32 m_hz)
{
	struct scpi_data_buf sdata;
	struct rk3368_mbox_msg mdata;
	struct __packed1 {
		u32 clk_rate;
	} tx_buf;
	struct __packed2 {
		u32 status;
		u32 round_rate;
	} rx_buf;

	tx_buf.clk_rate = (u32)m_hz;

	SCPI_SETUP_DBUF(sdata, mdata, SCPI_CL_DDR,
			SCPI_DDR_ROUND_RATE, tx_buf, rx_buf);
	if (scpi_execute_cmd(&sdata))
		return 0;

	return rx_buf.round_rate;
}
EXPORT_SYMBOL_GPL(scpi_ddr_round_rate);

int scpi_ddr_set_auto_self_refresh(u32 en)
{
	struct scpi_data_buf sdata;
	struct rk3368_mbox_msg mdata;
	struct __packed1 {
		u32 enable;
	} tx_buf;
	struct __packed2 {
		u32 status;
	} rx_buf;

	tx_buf.enable = (u32)en;

	SCPI_SETUP_DBUF(sdata, mdata, SCPI_CL_DDR,
			SCPI_DDR_AUTO_SELF_REFRESH, tx_buf, rx_buf);
	return scpi_execute_cmd(&sdata);
}
EXPORT_SYMBOL_GPL(scpi_ddr_set_auto_self_refresh);

int scpi_ddr_get_clk_rate(void)
{
	struct scpi_data_buf sdata;
	struct rk3368_mbox_msg mdata;
	struct __packed1 {
		u32 status;
	} tx_buf;
	struct __packed2 {
		u32 status;
		u32 clk_rate;
	} rx_buf;

	tx_buf.status = 0;
	SCPI_SETUP_DBUF(sdata, mdata, SCPI_CL_DDR,
			SCPI_DDR_GET_FREQ, tx_buf, rx_buf);
	if (scpi_execute_cmd(&sdata))
		return 0;

	return rx_buf.clk_rate;
}
EXPORT_SYMBOL_GPL(scpi_ddr_get_clk_rate);

int scpi_thermal_get_temperature(void)
{
	int ret;
	struct scpi_data_buf sdata;
	struct rk3368_mbox_msg mdata;
	struct __packed1 {
		u32 status;
	} tx_buf;

	struct __packed2 {
		u32 status;
		u32 tsadc_data;
	} rx_buf;

	tx_buf.status = 0;
	SCPI_SETUP_DBUF(sdata, mdata, SCPI_CL_THERMAL,
			SCPI_THERMAL_GET_TSADC_DATA, tx_buf, rx_buf);

	ret = scpi_execute_cmd(&sdata);
	if (ret) {
		pr_err("get temperature from MCU failed, ret=%d\n", ret);
		return ret;
	}

	return rx_buf.tsadc_data;
}
EXPORT_SYMBOL_GPL(scpi_thermal_get_temperature);

int scpi_thermal_set_clk_cycle(u32 cycle)
{
	struct scpi_data_buf sdata;
	struct rk3368_mbox_msg mdata;
	struct __packed1 {
		u32 clk_cycle;
	} tx_buf;

	struct __packed2 {
		u32 status;
	} rx_buf;

	tx_buf.clk_cycle = cycle;
	SCPI_SETUP_DBUF(sdata, mdata, SCPI_CL_THERMAL,
			SCPI_THERMAL_SET_TSADC_CYCLE, tx_buf, rx_buf);

	return scpi_execute_cmd(&sdata);
}
EXPORT_SYMBOL_GPL(scpi_thermal_set_clk_cycle);

static struct of_device_id mobx_scpi_of_match[] = {
	{ .compatible = "rockchip,rk3368-scpi-legacy"},
	{ },
};
MODULE_DEVICE_TABLE(of, mobx_scpi_of_match);

static int mobx_scpi_probe(struct platform_device *pdev)
{
	int ret = 0;
	int retry = 3;
	int val = 0;
	struct scpi_mcu_ver mcu_ver;
	int check_version = 1; /*0: not check version, 1: check version*/

	the_scpi_device = &pdev->dev;

	/* try to get mboxes chan nums from DT */
	if (of_property_read_u32((&pdev->dev)->of_node, "chan-nums", &val)) {
		dev_err(&pdev->dev, "parse mboxes chan-nums failed\n");
		ret = -EINVAL;
		goto exit;
	}

	max_chan_num = val;

	/* try to check up with SCPI version from MCU */
	while ((retry--) && (check_version != 0)) {
		memset(&mcu_ver, 0, sizeof(mcu_ver));

		ret = scpi_get_version(SCPI_VERSION, &mcu_ver);
		if ((ret == 0) && (mcu_ver.scpi_ver == SCPI_VERSION))
			break;
	}

	if ((retry <= 0) && (check_version != 0)) {
		dev_err(&pdev->dev,
			"Scpi verison not match:kernel ver:0x%x, MCU ver:0x%x, ret=%d\n",
			SCPI_VERSION, mcu_ver.scpi_ver, ret);
		ret = -EIO;
		goto exit;
	}

	dev_info(&pdev->dev, "Scpi initialize, version: 0x%x, chan nums: %d\n",
		 SCPI_VERSION, val);

	if (check_version)
		dev_info(&pdev->dev, "MCU version: %s\n", mcu_ver.mcu_ver);

	return 0;
exit:
	the_scpi_device = NULL;
	return ret;
}

static struct platform_driver mbox_scpi_driver = {
	.probe	= mobx_scpi_probe,
	.driver = {
		.name = "mbox-scpi",
		.of_match_table = of_match_ptr(mobx_scpi_of_match),
	},
};

static int __init rockchip_mbox_scpi_init(void)
{
	return platform_driver_register(&mbox_scpi_driver);
}
subsys_initcall(rockchip_mbox_scpi_init);

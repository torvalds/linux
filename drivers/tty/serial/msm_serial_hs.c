// SPDX-License-Identifier: GPL-2.0-only
/* drivers/serial/msm_serial_hs.c
 *
 * MSM 7k High speed uart driver
 *
 * Copyright (c) 2008 Google Inc.
 * Copyright (c) 2007-2018, 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * Modified: Nick Pelly <npelly@google.com>
 *
 * Has optional support for uart power management independent of linux
 * suspend/resume:
 *
 * RX wakeup.
 * UART wakeup can be triggered by RX activity (using a wakeup GPIO on the
 * UART RX pin). This should only be used if there is not a wakeup
 * GPIO on the UART CTS, and the first RX byte is known (for example, with the
 * Bluetooth Texas Instruments HCILL protocol), since the first RX byte will
 * always be lost. RTS will be asserted even while the UART is off in this mode
 * of operation. See msm_serial_hs_platform_data.rx_wakeup_irq.
 */

#include <linux/module.h>

#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/atomic.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/dma-mapping.h>
#include <linux/tty_flip.h>
#include <linux/wait.h>
#include <linux/sysfs.h>
#include <linux/stat.h>
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/ipc_logging.h>
#include <asm/irq.h>
#include <linux/kthread.h>

#include <linux/msm-sps.h>
#include <linux/platform_data/msm_serial_hs.h>
#include <linux/interconnect.h>

#include "msm_serial_hs_hwreg.h"

#define PINCTRL_STATE_ACTIVE "active"
#define PINCTRL_STATE_SHUTDOWN "shutdown"

#define UART_SPS_CONS_PERIPHERAL 0
#define UART_SPS_PROD_PERIPHERAL 1

#define IPC_MSM_HS_LOG_STATE_PAGES 2
#define IPC_MSM_HS_LOG_USER_PAGES 2
#define IPC_MSM_HS_LOG_DATA_PAGES 3
#define UART_DMA_DESC_NR 8
#define BUF_DUMP_SIZE 32

/* If the debug_mask gets set to FATAL_LEV,
 * a fatal error has happened and further IPC logging
 * is disabled so that this problem can be detected
 */
enum {
	FATAL_LEV = 0U,
	ERR_LEV = 1U,
	WARN_LEV = 2U,
	INFO_LEV = 3U,
	DBG_LEV = 4U,
};

#define MSM_HS_DBG(x...) do { \
	if (msm_uport->ipc_debug_mask >= DBG_LEV) { \
		ipc_log_string(msm_uport->ipc_msm_hs_log_ctxt, x); \
	} \
} while (0)

#define MSM_HS_INFO(x...) do { \
	if (msm_uport->ipc_debug_mask >= INFO_LEV) {\
		ipc_log_string(msm_uport->ipc_msm_hs_log_ctxt, x); \
	} \
} while (0)

/* warnings and errors show up on console always */
#define MSM_HS_WARN(x...) do { \
	pr_warn(x); \
	if (msm_uport->ipc_msm_hs_log_ctxt && \
			msm_uport->ipc_debug_mask >= WARN_LEV) \
		ipc_log_string(msm_uport->ipc_msm_hs_log_ctxt, x); \
} while (0)

/* ERROR condition in the driver sets the hs_serial_debug_mask
 * to ERR_FATAL level, so that this message can be seen
 * in IPC logging. Further errors continue to log on the console
 */
#define MSM_HS_ERR(x...) do { \
	pr_err(x); \
	if (msm_uport->ipc_msm_hs_log_ctxt && \
			msm_uport->ipc_debug_mask >= ERR_LEV) { \
		ipc_log_string(msm_uport->ipc_msm_hs_log_ctxt, x); \
		msm_uport->ipc_debug_mask = FATAL_LEV; \
	} \
} while (0)

#define LOG_USR_MSG(ctx, x...) ipc_log_string(ctx, x)

/*
 * There are 3 different kind of UART Core available on MSM.
 * High Speed UART (i.e. Legacy HSUART), GSBI based HSUART
 * and BSLP based HSUART.
 */
enum uart_core_type {
	LEGACY_HSUART,
	GSBI_HSUART,
	BLSP_HSUART,
};

enum flush_reason {
	FLUSH_NONE,
	FLUSH_DATA_READY,
	FLUSH_DATA_INVALID,  /* values after this indicate invalid data */
	FLUSH_IGNORE,
	FLUSH_STOP,
	FLUSH_SHUTDOWN,
};

/*
 * SPS data structures to support HSUART with BAM
 * @sps_pipe - This struct defines BAM pipe descriptor
 * @sps_connect - This struct defines a connection's end point
 * @sps_register - This struct defines a event registration parameters
 */
struct msm_hs_sps_ep_conn_data {
	struct sps_pipe *pipe_handle;
	struct sps_connect config;
	struct sps_register_event event;
};

struct msm_hs_tx {
	bool dma_in_flight;    /* tx dma in progress */
	enum flush_reason flush;
	wait_queue_head_t wait;
	int tx_count;
	dma_addr_t dma_base;
	struct kthread_work kwork;
	struct kthread_worker kworker;
	struct task_struct *task;
	struct msm_hs_sps_ep_conn_data cons;
	struct timer_list tx_timeout_timer;
	void *ipc_tx_ctxt;
};

struct msm_hs_rx {
	enum flush_reason flush;
	wait_queue_head_t wait;
	dma_addr_t rbuffer;
	unsigned char *buffer;
	unsigned int buffer_pending;
	struct delayed_work flip_insert_work;
	struct kthread_work kwork;
	struct kthread_worker kworker;
	struct task_struct *task;
	struct msm_hs_sps_ep_conn_data prod;
	unsigned long queued_flag;
	unsigned long pending_flag;
	int rx_inx;
	struct sps_iovec iovec[UART_DMA_DESC_NR]; /* track descriptors */
	void *ipc_rx_ctxt;
};
enum buffer_states {
	NONE_PENDING = 0x0,
	FIFO_OVERRUN = 0x1,
	PARITY_ERROR = 0x2,
	CHARS_NORMAL = 0x4,
};

enum msm_hs_pm_state {
	MSM_HS_PM_ACTIVE,
	MSM_HS_PM_SUSPENDED,
	MSM_HS_PM_SYS_SUSPENDED,
};

/* optional low power wakeup, typically on a GPIO RX irq */
struct msm_hs_wakeup {
	int irq;  /* < 0 indicates low power wakeup disabled */
	unsigned char ignore;  /* bool */

	/* bool: inject char into rx tty on wakeup */
	bool inject_rx;
	unsigned char rx_to_inject;
	bool enabled;
};

struct msm_hs_port {
	struct uart_port uport;
	unsigned long imr_reg;  /* shadow value of UARTDM_IMR */
	struct clk *clk;
	struct clk *pclk;
	struct msm_hs_tx tx;
	struct msm_hs_rx rx;
	atomic_t resource_count;
	struct msm_hs_wakeup wakeup;

	struct dentry *loopback_dir;
	struct work_struct clock_off_w; /* work for actual clock off */
	struct workqueue_struct *hsuart_wq; /* hsuart workqueue */
	struct mutex mtx; /* resource access mutex */
	enum uart_core_type uart_type;
	unsigned long bam_handle;
	resource_size_t bam_mem;
	int bam_irq;
	unsigned char __iomem *bam_base;
	unsigned int bam_tx_ep_pipe_index;
	unsigned int bam_rx_ep_pipe_index;
	/* struct sps_event_notify is an argument passed when triggering a
	 * callback event object registered for an SPS connection end point.
	 */
	struct sps_event_notify notify;
	/* bus client handler */
	u32 bus_perf_client;
	/* BLSP UART required ICC BUS voting */
	struct icc_path	*icc_path;
	bool rx_bam_inprogress;
	wait_queue_head_t bam_disconnect_wait;
	bool use_pinctrl;
	struct pinctrl *pinctrl;
	struct pinctrl_state *gpio_state_active;
	struct pinctrl_state *gpio_state_suspend;
	struct pinctrl_state *gpio_state_shutdown;
	bool flow_control;
	enum msm_hs_pm_state pm_state;
	atomic_t client_count;
	bool obs; /* out of band sleep flag */
	atomic_t client_req_state;
	void *ipc_msm_hs_log_ctxt;
	void *ipc_msm_hs_pwr_ctxt;
	int ipc_debug_mask;
};

static const struct of_device_id msm_hs_match_table[] = {
	{ .compatible = "qcom,msm-hsuart-v14"},
	{}
};


#define MSM_UARTDM_BURST_SIZE 16   /* DM burst size (in bytes) */
#define UARTDM_TX_BUF_SIZE UART_XMIT_SIZE
#define UARTDM_RX_BUF_SIZE 512
#define RETRY_TIMEOUT 5
#define UARTDM_NR 256
#define BAM_PIPE_MIN 0
#define BAM_PIPE_MAX 11
#define BUS_SCALING 1
#define BUS_RESET 0
#define RX_FLUSH_COMPLETE_TIMEOUT 300 /* In jiffies */
#define BLSP_UART_CLK_FMAX 63160000

/* Interconnect path bandwidths (each times 1000 bytes per second) */
#define BLSP_MEMORY_AVG  500
#define BLSP_MEMORY_PEAK 800

static struct dentry *debug_base;
static struct platform_driver msm_serial_hs_platform_driver;
static struct uart_driver msm_hs_driver;
static const struct uart_ops msm_hs_ops;
static void msm_hs_start_rx_locked(struct uart_port *uport);
static void msm_serial_hs_rx_work(struct kthread_work *work);
static void flip_insert_work(struct work_struct *work);
static struct msm_hs_port *msm_hs_get_hs_port(int port_index);
static void msm_hs_queue_rx_desc(struct msm_hs_port *msm_uport);
static int disconnect_rx_endpoint(struct msm_hs_port *msm_uport);
static int msm_hs_pm_resume(struct device *dev);

#define UARTDM_TO_MSM(uart_port) \
	container_of((uart_port), struct msm_hs_port, uport)

static int msm_hs_ioctl(struct uart_port *uport, unsigned int cmd,
						unsigned long arg)
{
	int ret = 0, state = 1;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	if (!msm_uport)
		return -ENODEV;

	switch (cmd) {
	case MSM_ENABLE_UART_CLOCK: {
		ret = msm_hs_request_clock_on(&msm_uport->uport);
		break;
	}
	case MSM_DISABLE_UART_CLOCK: {
		ret = msm_hs_request_clock_off(&msm_uport->uport);
		break;
	}
	case MSM_GET_UART_CLOCK_STATUS: {
		/* Return value 0 - UART CLOCK is OFF
		 * Return value 1 - UART CLOCK is ON
		 */

		if (msm_uport->pm_state != MSM_HS_PM_ACTIVE)
			state = 0;
		ret = state;
		MSM_HS_INFO("%s():GET UART CLOCK STATUS: cmd=%d state=%d\n",
			__func__, cmd, state);
		break;
	}
	default: {
		MSM_HS_INFO("%s():Unknown cmd specified: cmd=%d\n", __func__,
			   cmd);
		ret = -ENOIOCTLCMD;
		break;
	}
	}

	return ret;
}

/*
 * This function is called initially during probe and then
 * through the runtime PM framework. The function directly calls
 * resource APIs to enable them.
 */

static int msm_hs_clk_bus_vote(struct msm_hs_port *msm_uport)
{
	int rc = 0;
	int ret;

	if (msm_uport->icc_path)
		ret = icc_set_bw(msm_uport->icc_path,
				 BLSP_MEMORY_AVG, BLSP_MEMORY_PEAK);
	/* Turn on core clk and iface clk */
	if (msm_uport->pclk) {
		rc = clk_prepare_enable(msm_uport->pclk);
		if (rc) {
			dev_err(msm_uport->uport.dev,
				"%s(): Could not turn on pclk [%d]\n",
				__func__, rc);
			goto busreset;
		}
	}
	rc = clk_prepare_enable(msm_uport->clk);
	if (rc) {
		dev_err(msm_uport->uport.dev,
			"%s(): Could not turn on core clk [%d]\n",
			__func__, rc);
		goto core_unprepare;
	}
	MSM_HS_DBG("%s(): Clock ON successful\n", __func__);
	return rc;
core_unprepare:
	clk_disable_unprepare(msm_uport->pclk);
busreset:
	if (msm_uport->icc_path)
		ret = icc_set_bw(msm_uport->icc_path, 0, 0);
	return rc;
}

/*
 * This function is called initially during probe and then
 * through the runtime PM framework. The function directly calls
 * resource apis to disable them.
 */
static void msm_hs_clk_bus_unvote(struct msm_hs_port *msm_uport)
{
	int ret;

	clk_disable_unprepare(msm_uport->clk);
	if (msm_uport->pclk)
		clk_disable_unprepare(msm_uport->pclk);
	if (msm_uport->icc_path)
		ret = icc_set_bw(msm_uport->icc_path, 0, 0);
	MSM_HS_DBG("%s(): Clock OFF successful\n", __func__);
}

 /* Remove vote for resources when done */
static void msm_hs_resource_unvote(struct msm_hs_port *msm_uport)
{
	struct uart_port *uport = &(msm_uport->uport);
	int rc = atomic_read(&msm_uport->resource_count);

	MSM_HS_DBG("%s(): power usage count %d\n", __func__, rc);
	if (rc <= 0) {
		MSM_HS_WARN("%s(): rc zero, bailing\n", __func__);
		WARN_ON(1);
		return;
	}
	atomic_dec(&msm_uport->resource_count);
	pm_runtime_mark_last_busy(uport->dev);
	pm_runtime_put_autosuspend(uport->dev);
}

 /* Vote for resources before accessing them */
static void msm_hs_resource_vote(struct msm_hs_port *msm_uport)
{
	int ret;
	struct uart_port *uport = &(msm_uport->uport);

	ret = pm_runtime_get_sync(uport->dev);
	if (ret < 0 || msm_uport->pm_state != MSM_HS_PM_ACTIVE) {
		MSM_HS_WARN("%s():%s runtime PM CB not invoked ret:%d st:%d\n",
			__func__, dev_name(uport->dev), ret,
					msm_uport->pm_state);
		msm_hs_pm_resume(uport->dev);
	}
	atomic_inc(&msm_uport->resource_count);
}

/* Check if the uport line number matches with user id stored in pdata.
 * User id information is stored during initialization. This function
 * ensues that the same device is selected
 */

static struct msm_hs_port *get_matching_hs_port(struct platform_device *pdev)
{
	struct msm_serial_hs_platform_data *pdata = pdev->dev.platform_data;
	struct msm_hs_port *msm_uport = msm_hs_get_hs_port(pdev->id);

	if ((!msm_uport) || (msm_uport->uport.line != pdev->id
	   && msm_uport->uport.line != pdata->userid)) {
		pr_err("uport line number mismatch\n");
		WARN_ON(1);
		return NULL;
	}

	return msm_uport;
}

static ssize_t clock_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int state = 1;
	ssize_t ret = 0;
	struct platform_device *pdev = container_of(dev, struct
						    platform_device, dev);
	struct msm_hs_port *msm_uport = get_matching_hs_port(pdev);

	/* This check should not fail */
	if (msm_uport) {
		if (msm_uport->pm_state != MSM_HS_PM_ACTIVE)
			state = 0;
		ret = scnprintf(buf, PAGE_SIZE, "%d\n", state);
	}
	return ret;
}

static ssize_t clock_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	int state;
	ssize_t ret = 0;
	struct platform_device *pdev = container_of(dev, struct
						    platform_device, dev);
	struct msm_hs_port *msm_uport = get_matching_hs_port(pdev);

	/* This check should not fail */
	if (msm_uport) {
		state = buf[0] - '0';
		switch (state) {
		case 0:
			MSM_HS_DBG("%s(): Request clock OFF\n", __func__);
			msm_hs_request_clock_off(&msm_uport->uport);
			ret = count;
			break;
		case 1:
			MSM_HS_DBG("%s(): Request clock ON\n", __func__);
			msm_hs_request_clock_on(&msm_uport->uport);
			ret = count;
			break;
		default:
			ret = -EINVAL;
		}
	}
	return ret;
}

static DEVICE_ATTR_RW(clock);

static ssize_t debug_mask_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;
	struct platform_device *pdev = container_of(dev, struct
						    platform_device, dev);
	struct msm_hs_port *msm_uport = get_matching_hs_port(pdev);

	/* This check should not fail */
	if (msm_uport)
		ret = scnprintf(buf, sizeof(int), "%u\n",
					msm_uport->ipc_debug_mask);
	return ret;
}

static ssize_t debug_mask_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct platform_device *pdev = container_of(dev, struct
						    platform_device, dev);
	struct msm_hs_port *msm_uport = get_matching_hs_port(pdev);

	/* This check should not fail */
	if (msm_uport) {
		msm_uport->ipc_debug_mask = buf[0] - '0';
		if (msm_uport->ipc_debug_mask < FATAL_LEV ||
				msm_uport->ipc_debug_mask > DBG_LEV) {
			/* set to default level */
			msm_uport->ipc_debug_mask = INFO_LEV;
			MSM_HS_ERR("Range is 0 to 4;Set to default level 3\n");
			return -EINVAL;
		}
	}
	return count;
}

static DEVICE_ATTR_RW(debug_mask);

static inline bool is_use_low_power_wakeup(struct msm_hs_port *msm_uport)
{
	return msm_uport->wakeup.irq > 0;
}

static inline unsigned int msm_hs_read(struct uart_port *uport,
				       unsigned int index)
{
	return readl_relaxed(uport->membase + index);
}

static inline void msm_hs_write(struct uart_port *uport, unsigned int index,
				 unsigned int value)
{
	writel_relaxed(value, uport->membase + index);
}

static int sps_rx_disconnect(struct sps_pipe *sps_pipe_handler)
{
	struct sps_connect config;
	int ret;

	ret = sps_get_config(sps_pipe_handler, &config);
	if (ret) {
		pr_err("%s(): sps_get_config() failed ret %d\n", __func__, ret);
		return ret;
	}
	config.options |= SPS_O_POLL;
	ret = sps_set_config(sps_pipe_handler, &config);
	if (ret) {
		pr_err("%s(): sps_set_config() failed ret %d\n", __func__, ret);
		return ret;
	}
	return sps_disconnect(sps_pipe_handler);
}

static void hex_dump_ipc(struct msm_hs_port *msm_uport, void *ipc_ctx,
			char *prefix, char *string, u64 addr, int size)

{
	char buf[(BUF_DUMP_SIZE * 3) + 2];
	int len = 0;

	len = min(size, BUF_DUMP_SIZE);
	/*
	 * Print upto 32 data bytes, 32 bytes per line, 1 byte at a time and
	 * don't include the ASCII text at the end of the buffer.
	 */
	hex_dump_to_buffer(string, len, 32, 1, buf, sizeof(buf), false);
	ipc_log_string(ipc_ctx, "%s[0x%.10x:%d] : %s", prefix,
					(unsigned int)addr, size, buf);
}

/*
 * This API read and provides UART Core registers information.
 */
static void dump_uart_hs_registers(struct msm_hs_port *msm_uport)
{
	struct uart_port *uport = &(msm_uport->uport);

	if (msm_uport->pm_state != MSM_HS_PM_ACTIVE) {
		MSM_HS_INFO("%s():Failed clocks are off, resource_count %d\n",
			__func__, atomic_read(&msm_uport->resource_count));
		return;
	}

	MSM_HS_DBG(
	"MR1:%x MR2:%x TFWR:%x RFWR:%x DMEN:%x IMR:%x MISR:%x NCF_TX:%x\n",
	msm_hs_read(uport, UART_DM_MR1),
	msm_hs_read(uport, UART_DM_MR2),
	msm_hs_read(uport, UART_DM_TFWR),
	msm_hs_read(uport, UART_DM_RFWR),
	msm_hs_read(uport, UART_DM_DMEN),
	msm_hs_read(uport, UART_DM_IMR),
	msm_hs_read(uport, UART_DM_MISR),
	msm_hs_read(uport, UART_DM_NCF_TX));
	MSM_HS_INFO("SR:%x ISR:%x DMRX:%x RX_SNAP:%x TXFS:%x RXFS:%x\n",
	msm_hs_read(uport, UART_DM_SR),
	msm_hs_read(uport, UART_DM_ISR),
	msm_hs_read(uport, UART_DM_DMRX),
	msm_hs_read(uport, UART_DM_RX_TOTAL_SNAP),
	msm_hs_read(uport, UART_DM_TXFS),
	msm_hs_read(uport, UART_DM_RXFS));
	MSM_HS_DBG("rx.flush:%u\n", msm_uport->rx.flush);
}

static int msm_serial_loopback_enable_set(void *data, u64 val)
{
	struct msm_hs_port *msm_uport = data;
	struct uart_port *uport = &(msm_uport->uport);
	unsigned long flags;
	int ret = 0;

	msm_hs_resource_vote(msm_uport);

	if (val) {
		spin_lock_irqsave(&uport->lock, flags);
		ret = msm_hs_read(uport, UART_DM_MR2);
		ret |= (UARTDM_MR2_LOOP_MODE_BMSK |
			UARTDM_MR2_RFR_CTS_LOOP_MODE_BMSK);
		msm_hs_write(uport, UART_DM_MR2, ret);
		spin_unlock_irqrestore(&uport->lock, flags);
	} else {
		spin_lock_irqsave(&uport->lock, flags);
		ret = msm_hs_read(uport, UART_DM_MR2);
		ret &= ~(UARTDM_MR2_LOOP_MODE_BMSK |
			UARTDM_MR2_RFR_CTS_LOOP_MODE_BMSK);
		msm_hs_write(uport, UART_DM_MR2, ret);
		spin_unlock_irqrestore(&uport->lock, flags);
	}
	/* Calling CLOCK API. Hence mb() requires here. */
	mb();

	msm_hs_resource_unvote(msm_uport);
	return 0;
}

static int msm_serial_loopback_enable_get(void *data, u64 *val)
{
	struct msm_hs_port *msm_uport = data;
	struct uart_port *uport = &(msm_uport->uport);
	unsigned long flags;
	int ret = 0;

	msm_hs_resource_vote(msm_uport);

	spin_lock_irqsave(&uport->lock, flags);
	ret = msm_hs_read(&msm_uport->uport, UART_DM_MR2);
	spin_unlock_irqrestore(&uport->lock, flags);

	msm_hs_resource_unvote(msm_uport);

	*val = (ret & UARTDM_MR2_LOOP_MODE_BMSK) ? 1 : 0;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(loopback_enable_fops, msm_serial_loopback_enable_get,
			msm_serial_loopback_enable_set, "%llu\n");

/*
 * msm_serial_hs debugfs node: <debugfs_root>/msm_serial_hs/loopback.<id>
 * writing 1 turns on internal loopback mode in HW. Useful for automation
 * test scripts.
 * writing 0 disables the internal loopback mode. Default is disabled.
 */
static void msm_serial_debugfs_init(struct msm_hs_port *msm_uport,
					   int id)
{
	char node_name[15];

	scnprintf(node_name, sizeof(node_name), "loopback.%d", id);
	msm_uport->loopback_dir = debugfs_create_file(node_name,
						0644,
						debug_base,
						msm_uport,
						&loopback_enable_fops);

	if (IS_ERR_OR_NULL(msm_uport->loopback_dir))
		MSM_HS_ERR("%s(): Cannot create loopback.%d debug entry\n",
							__func__, id);
}

static int msm_hs_remove(struct platform_device *pdev)
{

	struct msm_hs_port *msm_uport;
	struct device *dev;

	if (pdev->id < 0 || pdev->id >= UARTDM_NR) {
		pr_err("Invalid plaform device ID = %d\n", pdev->id);
		return -EINVAL;
	}

	msm_uport = get_matching_hs_port(pdev);
	if (!msm_uport)
		return -EINVAL;

	dev = msm_uport->uport.dev;
	sysfs_remove_file(&pdev->dev.kobj, &dev_attr_clock.attr);
	sysfs_remove_file(&pdev->dev.kobj, &dev_attr_debug_mask.attr);
	debugfs_remove(msm_uport->loopback_dir);

	dma_free_coherent(msm_uport->uport.dev,
			UART_DMA_DESC_NR * UARTDM_RX_BUF_SIZE,
			msm_uport->rx.buffer, msm_uport->rx.rbuffer);

	msm_uport->rx.buffer = NULL;
	msm_uport->rx.rbuffer = 0;

	destroy_workqueue(msm_uport->hsuart_wq);
	mutex_destroy(&msm_uport->mtx);

	uart_remove_one_port(&msm_hs_driver, &msm_uport->uport);
	clk_put(msm_uport->clk);
	if (msm_uport->pclk)
		clk_put(msm_uport->pclk);

	iounmap(msm_uport->uport.membase);

	return 0;
}


/* Connect a UART peripheral's SPS endpoint(consumer endpoint)
 *
 * Also registers a SPS callback function for the consumer
 * process with the SPS driver
 *
 * @uport - Pointer to uart uport structure
 *
 * @return - 0 if successful else negative value.
 *
 */

static int msm_hs_spsconnect_tx(struct msm_hs_port *msm_uport)
{
	int ret;
	struct uart_port *uport = &msm_uport->uport;
	struct msm_hs_tx *tx = &msm_uport->tx;
	struct sps_pipe *sps_pipe_handle = tx->cons.pipe_handle;
	struct sps_connect *sps_config = &tx->cons.config;
	struct sps_register_event *sps_event = &tx->cons.event;
	unsigned long flags;
	unsigned int data;

	if (tx->flush != FLUSH_SHUTDOWN) {
		MSM_HS_ERR("%s():Invalid flush state:%d\n",
			__func__, tx->flush);
		return 0;
	}

	/* Establish connection between peripheral and memory endpoint */
	ret = sps_connect(sps_pipe_handle, sps_config);
	if (ret) {
		MSM_HS_ERR("msm_serial_hs: sps_connect() failed for tx\n"
		"pipe_handle=0x%pK ret=%d", sps_pipe_handle, ret);
		return ret;
	}
	/* Register callback event for EOT (End of transfer) event. */
	ret = sps_register_event(sps_pipe_handle, sps_event);
	if (ret) {
		MSM_HS_ERR("msm_serial_hs: sps_connect() failed for tx\n"
		"pipe_handle=0x%pK ret=%d", sps_pipe_handle, ret);
		goto reg_event_err;
	}

	spin_lock_irqsave(&(msm_uport->uport.lock), flags);
	msm_uport->tx.flush = FLUSH_STOP;
	spin_unlock_irqrestore(&(msm_uport->uport.lock), flags);

	data = msm_hs_read(uport, UART_DM_DMEN);
	/* Enable UARTDM Tx BAM Interface */
	data |= UARTDM_TX_BAM_ENABLE_BMSK;
	msm_hs_write(uport, UART_DM_DMEN, data);

	msm_hs_write(uport, UART_DM_CR, RESET_TX);
	msm_hs_write(uport, UART_DM_CR, START_TX_BAM_IFC);
	msm_hs_write(uport, UART_DM_CR, UARTDM_CR_TX_EN_BMSK);

	MSM_HS_DBG("%s(): TX Connect\n", __func__);
	return 0;

reg_event_err:
	sps_disconnect(sps_pipe_handle);
	return ret;
}

/* Connect a UART peripheral's SPS endpoint(producer endpoint)
 *
 * Also registers a SPS callback function for the producer
 * process with the SPS driver
 *
 * @uport - Pointer to uart uport structure
 *
 * @return - 0 if successful else negative value.
 *
 */

static int msm_hs_spsconnect_rx(struct uart_port *uport)
{
	int ret;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);
	struct msm_hs_rx *rx = &msm_uport->rx;
	struct sps_pipe *sps_pipe_handle = rx->prod.pipe_handle;
	struct sps_connect *sps_config = &rx->prod.config;
	struct sps_register_event *sps_event = &rx->prod.event;
	unsigned long flags;

	/* Establish connection between peripheral and memory endpoint */
	ret = sps_connect(sps_pipe_handle, sps_config);
	if (ret) {
		MSM_HS_ERR("msm_serial_hs: sps_connect() failed for rx\n"
		"pipe_handle=0x%pK ret=%d", sps_pipe_handle, ret);
		return ret;
	}
	/* Register callback event for DESC_DONE event. */
	ret = sps_register_event(sps_pipe_handle, sps_event);
	if (ret) {
		MSM_HS_ERR("msm_serial_hs: sps_connect() failed for rx\n"
		"pipe_handle=0x%pK ret=%d", sps_pipe_handle, ret);
		goto reg_event_err;
	}
	spin_lock_irqsave(&uport->lock, flags);
	if (msm_uport->rx.pending_flag)
		MSM_HS_WARN("%s(): Buffers may be pending 0x%lx\n",
		__func__, msm_uport->rx.pending_flag);
	msm_uport->rx.queued_flag = 0;
	msm_uport->rx.pending_flag = 0;
	msm_uport->rx.rx_inx = 0;
	msm_uport->rx.flush = FLUSH_STOP;
	spin_unlock_irqrestore(&uport->lock, flags);
	MSM_HS_DBG("%s(): RX Connect\n", __func__);
	return 0;

reg_event_err:
	sps_disconnect(sps_pipe_handle);
	return ret;
}

/*
 * programs the UARTDM_CSR register with correct bit rates
 *
 * Interrupts should be disabled before we are called, as
 * we modify Set Baud rate
 * Set receive stale interrupt level, dependent on Bit Rate
 * Goal is to have around 8 ms before indicate stale.
 * roundup (((Bit Rate * .008) / 10) + 1
 */
static int msm_hs_set_bps_locked(struct uart_port *uport,
			       unsigned int bps)
{
	unsigned long rxstale;
	unsigned long data;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);
	long clk_round = 0;

	switch (bps) {
	case 300:
		msm_hs_write(uport, UART_DM_CSR, 0x00);
		rxstale = 1;
		break;
	case 600:
		msm_hs_write(uport, UART_DM_CSR, 0x11);
		rxstale = 1;
		break;
	case 1200:
		msm_hs_write(uport, UART_DM_CSR, 0x22);
		rxstale = 1;
		break;
	case 2400:
		msm_hs_write(uport, UART_DM_CSR, 0x33);
		rxstale = 1;
		break;
	case 4800:
		msm_hs_write(uport, UART_DM_CSR, 0x44);
		rxstale = 1;
		break;
	case 9600:
		msm_hs_write(uport, UART_DM_CSR, 0x55);
		rxstale = 2;
		break;
	case 14400:
		msm_hs_write(uport, UART_DM_CSR, 0x66);
		rxstale = 3;
		break;
	case 19200:
		msm_hs_write(uport, UART_DM_CSR, 0x77);
		rxstale = 4;
		break;
	case 28800:
		msm_hs_write(uport, UART_DM_CSR, 0x88);
		rxstale = 6;
		break;
	case 38400:
		msm_hs_write(uport, UART_DM_CSR, 0x99);
		rxstale = 8;
		break;
	case 57600:
		msm_hs_write(uport, UART_DM_CSR, 0xaa);
		rxstale = 16;
		break;
	case 76800:
		msm_hs_write(uport, UART_DM_CSR, 0xbb);
		rxstale = 16;
		break;
	case 115200:
		msm_hs_write(uport, UART_DM_CSR, 0xcc);
		rxstale = 31;
		break;
	case 230400:
		msm_hs_write(uport, UART_DM_CSR, 0xee);
		rxstale = 31;
		break;
	case 460800:
		msm_hs_write(uport, UART_DM_CSR, 0xff);
		rxstale = 31;
		break;
	case 4000000:
	case 3686400:
	case 3200000:
	case 3500000:
	case 3000000:
	case 2500000:
	case 2000000:
	case 1500000:
	case 1152000:
	case 1000000:
	case 921600:
		msm_hs_write(uport, UART_DM_CSR, 0xff);
		rxstale = 31;
		break;
	default:
		msm_hs_write(uport, UART_DM_CSR, 0xff);
		/* default to 9600 */
		bps = 9600;
		rxstale = 2;
		break;
	}
	/*
	 * uart baud rate depends on CSR and MND Values
	 * we are updating CSR before and then calling
	 * clk_set_rate which updates MND Values. Hence
	 * dsb requires here.
	 */
	mb();
	if (bps > 460800) {
		uport->uartclk = bps * 16;
		/* BLSP based UART supports maximum clock frequency
		 * of 63.16 Mhz. With this (63.16 Mhz) clock frequency
		 * UART can support baud rate of 3.94 Mbps which is
		 * equivalent to 4 Mbps.
		 * UART hardware is robust enough to handle this
		 * deviation to achieve baud rate ~4 Mbps.
		 */
		if (bps == 4000000) {
			clk_round = clk_round_rate(msm_uport->clk, BLSP_UART_CLK_FMAX);
			MSM_HS_INFO("%s(): baud:%u clk_round_rate:%lu uartclk:%u\n",
					__func__, bps, clk_round, uport->uartclk);
			if (clk_round < 0) {
				MSM_HS_ERR("%s(): clk_round_rate failed ret: %d\n",
					__func__, clk_round);
				return clk_round;
			}
			uport->uartclk = clk_round;
		}
	} else {
		uport->uartclk = 7372800;
	}

	if (clk_set_rate(msm_uport->clk, uport->uartclk)) {
		MSM_HS_WARN("Error setting clock rate on UART\n");
		WARN_ON(1);
	}

	data = rxstale & UARTDM_IPR_STALE_LSB_BMSK;
	data |= UARTDM_IPR_STALE_TIMEOUT_MSB_BMSK & (rxstale << 2);

	msm_hs_write(uport, UART_DM_IPR, data);
	/*
	 * It is suggested to do reset of transmitter and receiver after
	 * changing any protocol configuration. Here Baud rate and stale
	 * timeout are getting updated. Hence reset transmitter and receiver.
	 */
	msm_hs_write(uport, UART_DM_CR, RESET_TX);
	msm_hs_write(uport, UART_DM_CR, RESET_RX);

	return 0;
}


static void msm_hs_set_std_bps_locked(struct uart_port *uport,
			       unsigned int bps)
{
	unsigned long rxstale;
	unsigned long data;

	switch (bps) {
	case 9600:
		msm_hs_write(uport, UART_DM_CSR, 0x99);
		rxstale = 2;
		break;
	case 14400:
		msm_hs_write(uport, UART_DM_CSR, 0xaa);
		rxstale = 3;
		break;
	case 19200:
		msm_hs_write(uport, UART_DM_CSR, 0xbb);
		rxstale = 4;
		break;
	case 28800:
		msm_hs_write(uport, UART_DM_CSR, 0xcc);
		rxstale = 6;
		break;
	case 38400:
		msm_hs_write(uport, UART_DM_CSR, 0xdd);
		rxstale = 8;
		break;
	case 57600:
		msm_hs_write(uport, UART_DM_CSR, 0xee);
		rxstale = 16;
		break;
	case 115200:
		msm_hs_write(uport, UART_DM_CSR, 0xff);
		rxstale = 31;
		break;
	default:
		msm_hs_write(uport, UART_DM_CSR, 0x99);
		/* default to 9600 */
		bps = 9600;
		rxstale = 2;
		break;
	}

	data = rxstale & UARTDM_IPR_STALE_LSB_BMSK;
	data |= UARTDM_IPR_STALE_TIMEOUT_MSB_BMSK & (rxstale << 2);

	msm_hs_write(uport, UART_DM_IPR, data);
}

static void msm_hs_enable_flow_control(struct uart_port *uport, bool override)
{
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);
	unsigned int data;

	if (msm_uport->flow_control || override) {
		/* Enable RFR line */
		msm_hs_write(uport, UART_DM_CR, RFR_LOW);
		/* Enable auto RFR */
		data = msm_hs_read(uport, UART_DM_MR1);
		data |= UARTDM_MR1_RX_RDY_CTL_BMSK;
		msm_hs_write(uport, UART_DM_MR1, data);
		/* Ensure register IO completion */
		mb();
	}
}

static void msm_hs_disable_flow_control(struct uart_port *uport, bool override)
{
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);
	unsigned int data;

	/*
	 * Clear the Rx Ready Ctl bit - This ensures that
	 * flow control lines stop the other side from sending
	 * data while we change the parameters
	 */

	if (msm_uport->flow_control || override) {
		data = msm_hs_read(uport, UART_DM_MR1);
		/* disable auto ready-for-receiving */
		data &= ~UARTDM_MR1_RX_RDY_CTL_BMSK;
		msm_hs_write(uport, UART_DM_MR1, data);
		/* Disable RFR line */
		msm_hs_write(uport, UART_DM_CR, RFR_HIGH);
		/* Ensure register IO completion */
		mb();
	}
}

/*
 * termios :  new ktermios
 * oldtermios:  old ktermios previous setting
 *
 * Configure the serial port
 */
static void msm_hs_set_termios(struct uart_port *uport,
				   struct ktermios *termios,
				  const struct ktermios *oldtermios)
{
	unsigned int bps;
	unsigned long data;
	unsigned int c_cflag = termios->c_cflag;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);
	int ret;

	/**
	 * set_termios can be invoked from the framework when
	 * the clocks are off and the client has not had a chance
	 * to turn them on. Make sure that they are on
	 */
	msm_hs_resource_vote(msm_uport);
	mutex_lock(&msm_uport->mtx);
	msm_hs_write(uport, UART_DM_IMR, 0);

	msm_hs_disable_flow_control(uport, true);

	/*
	 * Disable Rx channel of UARTDM
	 * DMA Rx Stall happens if enqueue and flush of Rx command happens
	 * concurrently. Hence before changing the baud rate/protocol
	 * configuration and sending flush command to ADM, disable the Rx
	 * channel of UARTDM.
	 * Note: should not reset the receiver here immediately as it is not
	 * suggested to do disable/reset or reset/disable at the same time.
	 */
	data = msm_hs_read(uport, UART_DM_DMEN);
	/* Disable UARTDM RX BAM Interface */
	data &= ~UARTDM_RX_BAM_ENABLE_BMSK;
	msm_hs_write(uport, UART_DM_DMEN, data);

	/*
	 * Reset RX and TX.
	 * Resetting the RX enables it, therefore we must reset and disable.
	 */
	msm_hs_write(uport, UART_DM_CR, RESET_RX);
	msm_hs_write(uport, UART_DM_CR, UARTDM_CR_RX_DISABLE_BMSK);
	msm_hs_write(uport, UART_DM_CR, RESET_TX);

	/* 300 is the minimum baud support by the driver  */
	bps = uart_get_baud_rate(uport, termios, oldtermios, 200, 4000000);

	/* Temporary remapping  200 BAUD to 3.2 mbps */
	if (bps == 200)
		bps = 3200000;

	uport->uartclk = clk_get_rate(msm_uport->clk);
	if (!uport->uartclk)
		msm_hs_set_std_bps_locked(uport, bps);
	else {
		ret = msm_hs_set_bps_locked(uport, bps);
		if (ret < 0) {
			msm_hs_enable_flow_control(uport, false);
			msm_hs_resource_unvote(msm_uport);
			mutex_unlock(&msm_uport->mtx);
			dev_err(uport->dev, "Unable to set clk %lu\n",
				uport->uartclk);
			return;
		}
	}

	data = msm_hs_read(uport, UART_DM_MR2);
	data &= ~UARTDM_MR2_PARITY_MODE_BMSK;
	/* set parity */
	if (c_cflag & PARENB) {
		if (c_cflag & PARODD)
			data |= ODD_PARITY;
		else if (c_cflag & CMSPAR)
			data |= SPACE_PARITY;
		else
			data |= EVEN_PARITY;
	}

	/* Set bits per char */
	data &= ~UARTDM_MR2_BITS_PER_CHAR_BMSK;

	switch (c_cflag & CSIZE) {
	case CS5:
		data |= FIVE_BPC;
		break;
	case CS6:
		data |= SIX_BPC;
		break;
	case CS7:
		data |= SEVEN_BPC;
		break;
	default:
		data |= EIGHT_BPC;
		break;
	}
	/* stop bits */
	if (c_cflag & CSTOPB) {
		data |= STOP_BIT_TWO;
	} else {
		/* otherwise 1 stop bit */
		data |= STOP_BIT_ONE;
	}
	data |= UARTDM_MR2_ERROR_MODE_BMSK;
	/* write parity/bits per char/stop bit configuration */
	msm_hs_write(uport, UART_DM_MR2, data);

	uport->ignore_status_mask = termios->c_iflag & INPCK;
	uport->ignore_status_mask |= termios->c_iflag & IGNPAR;
	uport->ignore_status_mask |= termios->c_iflag & IGNBRK;

	uport->read_status_mask = (termios->c_cflag & CREAD);

	/* Set Transmit software time out */
	uart_update_timeout(uport, c_cflag, bps);

	/* Enable UARTDM Rx BAM Interface */
	data = msm_hs_read(uport, UART_DM_DMEN);
	data |= UARTDM_RX_BAM_ENABLE_BMSK;
	msm_hs_write(uport, UART_DM_DMEN, data);
	msm_hs_write(uport, UART_DM_CR, UARTDM_CR_RX_EN_BMSK);
	/* Issue TX,RX BAM Start IFC command */
	msm_hs_write(uport, UART_DM_CR, START_TX_BAM_IFC);
	msm_hs_write(uport, UART_DM_CR, START_RX_BAM_IFC);
	/* Ensure Register Writes Complete */
	mb();

	/* Configure HW flow control
	 * UART Core would see status of CTS line when it is sending data
	 * to remote uart to confirm that it can receive or not.
	 * UART Core would trigger RFR if it is not having any space with
	 * RX FIFO.
	 */
	/* Pulling RFR line high */
	msm_hs_write(uport, UART_DM_CR, RFR_LOW);
	data = msm_hs_read(uport, UART_DM_MR1);
	data &= ~(UARTDM_MR1_CTS_CTL_BMSK | UARTDM_MR1_RX_RDY_CTL_BMSK);
	if (c_cflag & CRTSCTS) {
		data |= UARTDM_MR1_CTS_CTL_BMSK;
		data |= UARTDM_MR1_RX_RDY_CTL_BMSK;
		msm_uport->flow_control = true;
	}
	msm_hs_write(uport, UART_DM_MR1, data);
	MSM_HS_INFO("%s(): Cflags 0x%x Baud %u\n", __func__, c_cflag, bps);

	mutex_unlock(&msm_uport->mtx);

	msm_hs_resource_unvote(msm_uport);
}

/*
 *  Standard API, Transmitter
 *  Any character in the transmit shift register is sent
 */
unsigned int msm_hs_tx_empty(struct uart_port *uport)
{
	unsigned int data;
	unsigned int isr;
	unsigned int ret = 0;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	msm_hs_resource_vote(msm_uport);
	data = msm_hs_read(uport, UART_DM_SR);
	isr = msm_hs_read(uport, UART_DM_ISR);
	msm_hs_resource_unvote(msm_uport);
	MSM_HS_INFO("%s(): SR:0x%x ISR:0x%x\n", __func__, data, isr);

	if (data & UARTDM_SR_TXEMT_BMSK) {
		ret = TIOCSER_TEMT;
	} else
		/*
		 * Add an extra sleep here because sometimes the framework's
		 * delay (based on baud rate) isn't good enough.
		 * Note that this won't happen during every port close, only
		 * on select occassions when the userspace does back to back
		 * write() and close().
		 */
		usleep_range(5000, 7000);

	return ret;
}
EXPORT_SYMBOL_GPL(msm_hs_tx_empty);

/*
 *  Standard API, Stop transmitter.
 *  Any character in the transmit shift register is sent as
 *  well as the current data mover transfer .
 */
static void msm_hs_stop_tx_locked(struct uart_port *uport)
{
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);
	struct msm_hs_tx *tx = &msm_uport->tx;

	tx->flush = FLUSH_STOP;
}

static int disconnect_rx_endpoint(struct msm_hs_port *msm_uport)
{
	struct msm_hs_rx *rx = &msm_uport->rx;
	struct sps_pipe *sps_pipe_handle = rx->prod.pipe_handle;
	int ret = 0;

	ret = sps_rx_disconnect(sps_pipe_handle);

	if (msm_uport->rx.pending_flag)
		MSM_HS_WARN("%s(): Buffers may be pending 0x%lx\n",
		__func__, msm_uport->rx.pending_flag);
	MSM_HS_DBG("%s(): clearing desc usage flag\n", __func__);
	msm_uport->rx.queued_flag = 0;
	msm_uport->rx.pending_flag = 0;
	msm_uport->rx.rx_inx = 0;

	if (ret)
		MSM_HS_ERR("%s(): sps_disconnect failed\n", __func__);
	msm_uport->rx.flush = FLUSH_SHUTDOWN;
	MSM_HS_DBG("%s(): Calling Completion\n", __func__);
	wake_up(&msm_uport->bam_disconnect_wait);
	MSM_HS_DBG("%s(): Done Completion\n", __func__);
	wake_up(&msm_uport->rx.wait);
	return ret;
}

static int sps_tx_disconnect(struct msm_hs_port *msm_uport)
{
	struct uart_port *uport = &msm_uport->uport;
	struct msm_hs_tx *tx = &msm_uport->tx;
	struct sps_pipe *tx_pipe = tx->cons.pipe_handle;
	unsigned long flags;
	int ret = 0;

	if (msm_uport->tx.flush == FLUSH_SHUTDOWN) {
		MSM_HS_DBG("%s(): pipe already disonnected\n", __func__);
		return ret;
	}

	ret = sps_disconnect(tx_pipe);

	if (ret) {
		MSM_HS_ERR("%s(): sps_disconnect failed %d\n", __func__, ret);
		return ret;
	}

	spin_lock_irqsave(&uport->lock, flags);
	msm_uport->tx.flush = FLUSH_SHUTDOWN;
	spin_unlock_irqrestore(&uport->lock, flags);

	MSM_HS_DBG("%s(): TX Disconnect\n", __func__);
	return ret;
}

static void msm_hs_disable_rx(struct uart_port *uport)
{
	unsigned int data;

	data = msm_hs_read(uport, UART_DM_DMEN);
	data &= ~UARTDM_RX_BAM_ENABLE_BMSK;
	msm_hs_write(uport, UART_DM_DMEN, data);
}

/*
 *  Standard API, Stop receiver as soon as possible.
 *
 *  Function immediately terminates the operation of the
 *  channel receiver and any incoming characters are lost. None
 *  of the receiver status bits are affected by this command and
 *  characters that are already in the receive FIFO there.
 */
static void msm_hs_stop_rx_locked(struct uart_port *uport)
{
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	if (msm_uport->pm_state != MSM_HS_PM_ACTIVE)
		MSM_HS_WARN("%s(): Clocks are off\n", __func__);
	else
		msm_hs_disable_rx(uport);

	if (msm_uport->rx.flush == FLUSH_NONE)
		msm_uport->rx.flush = FLUSH_STOP;
}

static void msm_hs_disconnect_rx(struct uart_port *uport)
{
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	msm_hs_disable_rx(uport);
	/* Disconnect the BAM RX pipe */
	if (msm_uport->rx.flush == FLUSH_NONE)
		msm_uport->rx.flush = FLUSH_STOP;
	disconnect_rx_endpoint(msm_uport);
	MSM_HS_DBG("%s(): rx->flush %d\n", __func__, msm_uport->rx.flush);
}

/* Tx timeout callback function */
void tx_timeout_handler(struct timer_list *arg)
{
	struct msm_hs_port *msm_uport = from_timer(msm_uport, arg,
		tx.tx_timeout_timer);
	struct uart_port *uport = &msm_uport->uport;
	int isr;

	if (msm_uport->pm_state != MSM_HS_PM_ACTIVE) {
		MSM_HS_WARN("%s(): clocks are off\n", __func__);
		return;
	}

	isr = msm_hs_read(uport, UART_DM_ISR);
	if (UARTDM_ISR_CURRENT_CTS_BMSK & isr)
		MSM_HS_WARN("%s(): CTS Disabled, ISR 0x%x\n", __func__, isr);
	dump_uart_hs_registers(msm_uport);
}

/*  Transmit the next chunk of data */
static void msm_hs_submit_tx_locked(struct uart_port *uport)
{
	int left;
	int tx_count;
	int aligned_tx_count;
	dma_addr_t src_addr;
	dma_addr_t aligned_src_addr;
	u32 flags = SPS_IOVEC_FLAG_EOT | SPS_IOVEC_FLAG_INT;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);
	struct msm_hs_tx *tx = &msm_uport->tx;
	struct circ_buf *tx_buf = &msm_uport->uport.state->xmit;
	struct sps_pipe *sps_pipe_handle;
	int ret;

	if (uart_circ_empty(tx_buf) || uport->state->port.tty->flow.stopped) {
		tx->dma_in_flight = false;
		msm_hs_stop_tx_locked(uport);
		return;
	}

	tx_count = uart_circ_chars_pending(tx_buf);

	if (tx_count > UARTDM_TX_BUF_SIZE)
		tx_count = UARTDM_TX_BUF_SIZE;

	left = UART_XMIT_SIZE - tx_buf->tail;

	if (tx_count > left)
		tx_count = left;

	src_addr = tx->dma_base + tx_buf->tail;
	/* Mask the src_addr to align on a cache
	 * and add those bytes to tx_count
	 */
	aligned_src_addr = src_addr & ~(dma_get_cache_alignment() - 1);
	aligned_tx_count = tx_count + src_addr - aligned_src_addr;

	dma_sync_single_for_device(uport->dev, aligned_src_addr,
			aligned_tx_count, DMA_TO_DEVICE);

	tx->tx_count = tx_count;

	hex_dump_ipc(msm_uport, tx->ipc_tx_ctxt, "Tx",
			&tx_buf->buf[tx_buf->tail], (u64)src_addr, tx_count);
	sps_pipe_handle = tx->cons.pipe_handle;

	/* Set 1 second timeout */
	mod_timer(&tx->tx_timeout_timer,
		jiffies + msecs_to_jiffies(MSEC_PER_SEC));
	/* Queue transfer request to SPS */
	ret = sps_transfer_one(sps_pipe_handle, src_addr, tx_count,
				msm_uport, flags);

	MSM_HS_DBG("%s():Enqueue Tx Cmd, ret %d\n", __func__, ret);
}

/* This function queues the rx descriptor for BAM transfer */
static void msm_hs_post_rx_desc(struct msm_hs_port *msm_uport, int inx)
{
	u32 flags = SPS_IOVEC_FLAG_INT;
	struct msm_hs_rx *rx = &msm_uport->rx;
	int ret;

	phys_addr_t rbuff_addr = rx->rbuffer + (UARTDM_RX_BUF_SIZE * inx);
	u8 *virt_addr = rx->buffer + (UARTDM_RX_BUF_SIZE * inx);

	MSM_HS_DBG("%s(): %d:Queue desc %d, 0x%llx, base 0x%llx virtaddr %pK\n",
		__func__, msm_uport->uport.line, inx,
		(u64)rbuff_addr, (u64)rx->rbuffer, virt_addr);

	rx->iovec[inx].size = 0;
	ret = sps_transfer_one(rx->prod.pipe_handle, rbuff_addr,
		UARTDM_RX_BUF_SIZE, msm_uport, flags);

	if (ret)
		MSM_HS_ERR("Error processing descriptor %d\n", ret);
}

/* Update the rx descriptor index to specify the next one to be processed */
static void msm_hs_mark_next(struct msm_hs_port *msm_uport, int inx)
{
	struct msm_hs_rx *rx = &msm_uport->rx;
	int prev;

	inx %= UART_DMA_DESC_NR;
	MSM_HS_DBG("%s(): inx %d, pending 0x%lx\n", __func__, inx,
		rx->pending_flag);

	if (!inx)
		prev = UART_DMA_DESC_NR - 1;
	else
		prev = inx - 1;

	if (!test_bit(prev, &rx->pending_flag))
		msm_uport->rx.rx_inx = inx;
	MSM_HS_DBG("%s(): prev %d pending flag 0x%lx, next %d\n", __func__,
		prev, rx->pending_flag, msm_uport->rx.rx_inx);
}

/*
 *	Queue the rx descriptor that has just been processed or
 *	all of them if queueing for the first time
 */
static void msm_hs_queue_rx_desc(struct msm_hs_port *msm_uport)
{
	struct msm_hs_rx *rx = &msm_uport->rx;
	int i, flag = 0;

	/* At first, queue all, if not, queue only one */
	if (rx->queued_flag || rx->pending_flag) {
		if (!test_bit(rx->rx_inx, &rx->queued_flag) &&
		    !test_bit(rx->rx_inx, &rx->pending_flag)) {
			msm_hs_post_rx_desc(msm_uport, rx->rx_inx);
			set_bit(rx->rx_inx, &rx->queued_flag);
			MSM_HS_DBG("%s(): Set Queued Bit %d\n",
				__func__, rx->rx_inx);
		} else
			MSM_HS_ERR("%s(): rx_inx pending or queued\n",
				 __func__);
		return;
	}

	for (i = 0; i < UART_DMA_DESC_NR; i++) {
		if (!test_bit(i, &rx->queued_flag) &&
		!test_bit(i, &rx->pending_flag)) {
			MSM_HS_DBG("%s(): Calling post rx %d\n", __func__, i);
			msm_hs_post_rx_desc(msm_uport, i);
			set_bit(i, &rx->queued_flag);
			flag = 1;
		}
	}

	if (!flag)
		MSM_HS_ERR("%s(): error queueing descriptor\n", __func__);
}

/* Start to receive the next chunk of data */
static void msm_hs_start_rx_locked(struct uart_port *uport)
{
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);
	struct msm_hs_rx *rx = &msm_uport->rx;
	unsigned int buffer_pending = msm_uport->rx.buffer_pending;
	unsigned int data;

	if (msm_uport->pm_state != MSM_HS_PM_ACTIVE) {
		MSM_HS_WARN("%s(): Clocks are off\n", __func__);
		return;
	}
	if (rx->pending_flag) {
		MSM_HS_INFO("%s(): Rx Cmd got executed, wait for rx_tlet\n",
								 __func__);
		rx->flush = FLUSH_IGNORE;
		return;
	}
	if (buffer_pending)
		MSM_HS_ERR("Error: rx started in buffer state =%x\n",
			buffer_pending);

	msm_hs_write(uport, UART_DM_CR, RESET_STALE_INT);
	msm_hs_write(uport, UART_DM_DMRX, UARTDM_RX_BUF_SIZE);
	msm_hs_write(uport, UART_DM_CR, STALE_EVENT_ENABLE);
	/*
	 * Enable UARTDM Rx Interface as previously it has been
	 * disable in set_termios before configuring baud rate.
	 */
	data = msm_hs_read(uport, UART_DM_DMEN);
	/* Enable UARTDM Rx BAM Interface */
	data |= UARTDM_RX_BAM_ENABLE_BMSK;

	msm_hs_write(uport, UART_DM_DMEN, data);
	msm_hs_write(uport, UART_DM_IMR, msm_uport->imr_reg);
	/* Calling next DMOV API. Hence mb() here. */
	mb();

	/*
	 * RX-transfer will be automatically re-activated
	 * after last data of previous transfer was read.
	 */
	data = (RX_STALE_AUTO_RE_EN | RX_TRANS_AUTO_RE_ACTIVATE |
				RX_DMRX_CYCLIC_EN);
	msm_hs_write(uport, UART_DM_RX_TRANS_CTRL, data);
	/* Issue RX BAM Start IFC command */
	msm_hs_write(uport, UART_DM_CR, START_RX_BAM_IFC);
	/* Ensure register IO completion */
	mb();

	msm_uport->rx.flush = FLUSH_NONE;
	msm_uport->rx_bam_inprogress = true;
	msm_hs_queue_rx_desc(msm_uport);
	msm_uport->rx_bam_inprogress = false;
	wake_up(&msm_uport->rx.wait);
	MSM_HS_DBG("%s():Enqueue Rx Cmd\n", __func__);
}

static void flip_insert_work(struct work_struct *work)
{
	unsigned long flags;
	int retval;
	struct msm_hs_port *msm_uport =
		container_of(work, struct msm_hs_port,
			     rx.flip_insert_work.work);
	struct tty_struct *tty = msm_uport->uport.state->port.tty;

	spin_lock_irqsave(&msm_uport->uport.lock, flags);
	if (!tty || msm_uport->rx.flush == FLUSH_SHUTDOWN) {
		MSM_HS_ERR("%s() :Invalid driver state flush %d\n",
				__func__, msm_uport->rx.flush);
		spin_unlock_irqrestore(&msm_uport->uport.lock, flags);
		return;
	}

	if (msm_uport->rx.buffer_pending == NONE_PENDING) {
		MSM_HS_ERR("%s():Error: No buffer pending\n", __func__);
		spin_unlock_irqrestore(&msm_uport->uport.lock, flags);
		return;
	}
	if (msm_uport->rx.buffer_pending & FIFO_OVERRUN) {
		retval = tty_insert_flip_char(tty->port, 0, TTY_OVERRUN);
		if (retval)
			msm_uport->rx.buffer_pending &= ~FIFO_OVERRUN;
	}
	if (msm_uport->rx.buffer_pending & PARITY_ERROR) {
		retval = tty_insert_flip_char(tty->port, 0, TTY_PARITY);
		if (retval)
			msm_uport->rx.buffer_pending &= ~PARITY_ERROR;
	}
	if (msm_uport->rx.buffer_pending & CHARS_NORMAL) {
		int rx_count, rx_offset;

		rx_count = (msm_uport->rx.buffer_pending & 0xFFFF0000) >> 16;
		rx_offset = (msm_uport->rx.buffer_pending & 0xFFD0) >> 5;
		retval = tty_insert_flip_string(tty->port,
			msm_uport->rx.buffer +
			(msm_uport->rx.rx_inx * UARTDM_RX_BUF_SIZE)
			+ rx_offset, rx_count);
		msm_uport->rx.buffer_pending &= (FIFO_OVERRUN |
						 PARITY_ERROR);
		if (retval != rx_count)
			msm_uport->rx.buffer_pending |= CHARS_NORMAL |
				retval << 8 | (rx_count - retval) << 16;
	}
	if (msm_uport->rx.buffer_pending) {
		schedule_delayed_work(&msm_uport->rx.flip_insert_work,
				      msecs_to_jiffies(RETRY_TIMEOUT));
	} else if (msm_uport->rx.flush <= FLUSH_IGNORE) {
		MSM_HS_WARN("Pending buffers cleared, restarting\n");
		clear_bit(msm_uport->rx.rx_inx,
			&msm_uport->rx.pending_flag);
		msm_hs_start_rx_locked(&msm_uport->uport);
		msm_hs_mark_next(msm_uport, msm_uport->rx.rx_inx+1);
	}
	spin_unlock_irqrestore(&msm_uport->uport.lock, flags);
	tty_flip_buffer_push(tty->port);
}

static void msm_serial_hs_rx_work(struct kthread_work *work)
{
	int retval;
	int rx_count = 0;
	unsigned long status;
	unsigned long flags;
	unsigned int error_f = 0;
	struct uart_port *uport;
	struct msm_hs_port *msm_uport;
	unsigned int flush = FLUSH_DATA_INVALID;
	struct tty_struct *tty;
	struct sps_event_notify *notify;
	struct msm_hs_rx *rx;
	struct sps_pipe *sps_pipe_handle;
	struct platform_device *pdev;
	const struct msm_serial_hs_platform_data *pdata;

	msm_uport = container_of((struct kthread_work *) work,
				 struct msm_hs_port, rx.kwork);
	msm_hs_resource_vote(msm_uport);
	uport = &msm_uport->uport;
	tty = uport->state->port.tty;
	notify = &msm_uport->notify;
	rx = &msm_uport->rx;
	pdev = to_platform_device(uport->dev);
	pdata = pdev->dev.platform_data;

	spin_lock_irqsave(&uport->lock, flags);

	if (!tty || rx->flush == FLUSH_SHUTDOWN) {
		MSM_HS_ERR("%s():Invalid driver state flush %d\n",
				__func__, rx->flush);
		spin_unlock_irqrestore(&uport->lock, flags);
		msm_hs_resource_unvote(msm_uport);
		return;
	}

	/*
	 * Process all pending descs or if nothing is
	 * queued - called from termios
	 */
	while (!rx->buffer_pending &&
		(rx->pending_flag || !rx->queued_flag)) {
		MSM_HS_DBG("%s(): Loop P 0x%lx Q 0x%lx\n", __func__,
			rx->pending_flag, rx->queued_flag);

		status = msm_hs_read(uport, UART_DM_SR);


		/* overflow is not connect to data in a FIFO */
		if (unlikely((status & UARTDM_SR_OVERRUN_BMSK) &&
			     (uport->read_status_mask & CREAD))) {
			retval = tty_insert_flip_char(tty->port,
							0, TTY_OVERRUN);
			MSM_HS_WARN("%s(): RX Buffer Overrun Detected\n",
				__func__);
			if (!retval)
				msm_uport->rx.buffer_pending |= TTY_OVERRUN;
			uport->icount.buf_overrun++;
			error_f = 1;
		}

		if (!(uport->ignore_status_mask & INPCK))
			status = status & ~(UARTDM_SR_PAR_FRAME_BMSK);

		if (unlikely(status & UARTDM_SR_PAR_FRAME_BMSK)) {
			/* Can not tell diff between parity & frame error */
			MSM_HS_WARN("msm_serial_hs: parity error\n");
			uport->icount.parity++;
			error_f = 1;
			if (!(uport->ignore_status_mask & IGNPAR)) {
				retval = tty_insert_flip_char(tty->port,
							0, TTY_PARITY);
				if (!retval)
					msm_uport->rx.buffer_pending
								|= TTY_PARITY;
			}
		}

		if (unlikely(status & UARTDM_SR_RX_BREAK_BMSK)) {
			MSM_HS_DBG("msm_serial_hs: Rx break\n");
			uport->icount.brk++;
			error_f = 1;
			if (!(uport->ignore_status_mask & IGNBRK)) {
				retval = tty_insert_flip_char(tty->port,
								0, TTY_BREAK);
				if (!retval)
					msm_uport->rx.buffer_pending
								|= TTY_BREAK;
			}
		}

		if (error_f)
			msm_hs_write(uport, UART_DM_CR,	RESET_ERROR_STATUS);
		flush = msm_uport->rx.flush;
		if (flush == FLUSH_IGNORE)
			if (!msm_uport->rx.buffer_pending) {
				MSM_HS_DBG("%s(): calling start_rx_locked\n",
					__func__);
				msm_hs_start_rx_locked(uport);
			}
		if (flush >= FLUSH_DATA_INVALID)
			goto out;

		rx_count = msm_uport->rx.iovec[msm_uport->rx.rx_inx].size;
		hex_dump_ipc(msm_uport, rx->ipc_rx_ctxt, "Rx",
			(msm_uport->rx.buffer +
			(msm_uport->rx.rx_inx * UARTDM_RX_BUF_SIZE)),
			msm_uport->rx.iovec[msm_uport->rx.rx_inx].addr,
			rx_count);

		 /*
		  * We are in a spin locked context, spin lock taken at
		  * other places where these flags are updated
		  */
		if (0 != (uport->read_status_mask & CREAD)) {
			if (!test_bit(msm_uport->rx.rx_inx,
				&msm_uport->rx.pending_flag) &&
			    !test_bit(msm_uport->rx.rx_inx,
				&msm_uport->rx.queued_flag))
				MSM_HS_ERR("%s(): RX INX not set\n", __func__);
			else if (test_bit(msm_uport->rx.rx_inx,
					&msm_uport->rx.pending_flag) &&
				!test_bit(msm_uport->rx.rx_inx,
					&msm_uport->rx.queued_flag)) {
				MSM_HS_DBG("%s(): Clear Pending Bit %d\n",
					__func__, msm_uport->rx.rx_inx);

				retval = tty_insert_flip_string(tty->port,
					msm_uport->rx.buffer +
					(msm_uport->rx.rx_inx *
					UARTDM_RX_BUF_SIZE),
					rx_count);

				if (retval != rx_count) {
					MSM_HS_INFO("%s():ret %d rx_count %d\n",
						__func__, retval, rx_count);
					msm_uport->rx.buffer_pending |=
					CHARS_NORMAL | retval << 5 |
					(rx_count - retval) << 16;
				}
			} else
				MSM_HS_ERR("%s(): Error in inx %d\n", __func__,
					msm_uport->rx.rx_inx);
		}

		if (!msm_uport->rx.buffer_pending) {
			msm_uport->rx.flush = FLUSH_NONE;
			msm_uport->rx_bam_inprogress = true;
			sps_pipe_handle = rx->prod.pipe_handle;
			MSM_HS_DBG("Queuing bam descriptor\n");
			/* Queue transfer request to SPS */
			clear_bit(msm_uport->rx.rx_inx,
				&msm_uport->rx.pending_flag);
			msm_hs_queue_rx_desc(msm_uport);
			msm_hs_mark_next(msm_uport, msm_uport->rx.rx_inx+1);
			msm_hs_write(uport, UART_DM_CR, START_RX_BAM_IFC);
			msm_uport->rx_bam_inprogress = false;
			wake_up(&msm_uport->rx.wait);
		} else
			break;

	}
out:
	if (msm_uport->rx.buffer_pending) {
		MSM_HS_WARN("%s(): tty buffer exhausted,Stalling\n", __func__);
		schedule_delayed_work(&msm_uport->rx.flip_insert_work
				      , msecs_to_jiffies(RETRY_TIMEOUT));
	}
	/* tty_flip_buffer_push() might call msm_hs_start(), so unlock */
	spin_unlock_irqrestore(&uport->lock, flags);
	if (flush < FLUSH_DATA_INVALID)
		tty_flip_buffer_push(tty->port);
	msm_hs_resource_unvote(msm_uport);
}

static void msm_hs_start_tx_locked(struct uart_port *uport)
{
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);
	struct msm_hs_tx *tx = &msm_uport->tx;

	/* Bail if transfer in progress */
	if (tx->flush < FLUSH_STOP || tx->dma_in_flight) {
		MSM_HS_INFO("%s(): retry, flush %d, dma_in_flight %d\n",
			__func__, tx->flush, tx->dma_in_flight);
		return;
	}

	if (!tx->dma_in_flight) {
		tx->dma_in_flight = true;
		kthread_queue_work(&msm_uport->tx.kworker,
			&msm_uport->tx.kwork);
	}
}

/**
 * Callback notification from SPS driver
 *
 * This callback function gets triggered called from
 * SPS driver when requested SPS data transfer is
 * completed.
 *
 */

static void msm_hs_sps_tx_callback(struct sps_event_notify *notify)
{
	struct msm_hs_port *msm_uport =
		(struct msm_hs_port *)
		((struct sps_event_notify *)notify)->user;
	phys_addr_t addr = DESC_FULL_ADDR(notify->data.transfer.iovec.flags,
		notify->data.transfer.iovec.addr);

	msm_uport->notify = *notify;
	MSM_HS_INFO("tx_cb: addr=0x%pa, size=0x%x, flags=0x%x\n",
		&addr, notify->data.transfer.iovec.size,
		notify->data.transfer.iovec.flags);

	del_timer_sync(&msm_uport->tx.tx_timeout_timer);
	MSM_HS_DBG("%s(): Queue kthread work\n", __func__);
	kthread_queue_work(&msm_uport->tx.kworker, &msm_uport->tx.kwork);
}

static void msm_serial_hs_tx_work(struct kthread_work *work)
{
	unsigned long flags;
	struct msm_hs_port *msm_uport =
			container_of((struct kthread_work *)work,
			struct msm_hs_port, tx.kwork);
	struct uart_port *uport = &msm_uport->uport;
	struct circ_buf *tx_buf = &uport->state->xmit;
	struct msm_hs_tx *tx = &msm_uport->tx;

	/*
	 * Do the work buffer related work in BAM
	 * mode that is equivalent to legacy mode
	 */
	msm_hs_resource_vote(msm_uport);
	if (tx->flush >= FLUSH_STOP) {
		spin_lock_irqsave(&(msm_uport->uport.lock), flags);
		tx->flush = FLUSH_NONE;
		MSM_HS_DBG("%s(): calling submit_tx\n", __func__);
		msm_hs_submit_tx_locked(uport);
		spin_unlock_irqrestore(&(msm_uport->uport.lock), flags);
		msm_hs_resource_unvote(msm_uport);
		return;
	}

	spin_lock_irqsave(&(msm_uport->uport.lock), flags);
	if (!uart_circ_empty(tx_buf))
		tx_buf->tail = (tx_buf->tail +
		tx->tx_count) & ~UART_XMIT_SIZE;
	else
		MSM_HS_DBG("%s():circ buffer is empty\n", __func__);

	wake_up(&msm_uport->tx.wait);

	uport->icount.tx += tx->tx_count;

	/*
	 * Calling to send next chunk of data
	 * If the circ buffer is empty, we stop
	 * If the clock off was requested, the clock
	 * off sequence is kicked off
	 */
	 MSM_HS_DBG("%s(): calling submit_tx\n", __func__);
	 msm_hs_submit_tx_locked(uport);

	if (uart_circ_chars_pending(tx_buf) < WAKEUP_CHARS)
		uart_write_wakeup(uport);

	spin_unlock_irqrestore(&(msm_uport->uport.lock), flags);
	msm_hs_resource_unvote(msm_uport);
}

static void
msm_hs_mark_proc_rx_desc(struct msm_hs_port *msm_uport,
			struct sps_event_notify *notify)
{
	struct msm_hs_rx *rx = &msm_uport->rx;
	phys_addr_t addr = DESC_FULL_ADDR(notify->data.transfer.iovec.flags,
		notify->data.transfer.iovec.addr);
	/* divide by UARTDM_RX_BUF_SIZE */
	int inx = (addr - rx->rbuffer) >> 9;

	set_bit(inx, &rx->pending_flag);
	clear_bit(inx, &rx->queued_flag);
	rx->iovec[inx] = notify->data.transfer.iovec;
	MSM_HS_DBG("Clear Q, Set P Bit %d, Q 0x%lx P 0x%lx\n",
		inx, rx->queued_flag, rx->pending_flag);
}

/**
 * Callback notification from SPS driver
 *
 * This callback function gets triggered called from
 * SPS driver when requested SPS data transfer is
 * completed.
 *
 */

static void msm_hs_sps_rx_callback(struct sps_event_notify *notify)
{

	struct msm_hs_port *msm_uport =
		(struct msm_hs_port *)
		((struct sps_event_notify *)notify)->user;
	struct uart_port *uport;
	unsigned long flags;
	struct msm_hs_rx *rx = &msm_uport->rx;
	phys_addr_t addr = DESC_FULL_ADDR(notify->data.transfer.iovec.flags,
		notify->data.transfer.iovec.addr);
	/* divide by UARTDM_RX_BUF_SIZE */
	int inx = (addr - rx->rbuffer) >> 9;

	uport = &(msm_uport->uport);
	msm_uport->notify = *notify;
	MSM_HS_INFO("rx_cb: addr=0x%pa, size=0x%x, flags=0x%x\n",
		&addr, notify->data.transfer.iovec.size,
		notify->data.transfer.iovec.flags);

	spin_lock_irqsave(&uport->lock, flags);
	msm_hs_mark_proc_rx_desc(msm_uport, notify);
	spin_unlock_irqrestore(&uport->lock, flags);

	if (msm_uport->rx.flush == FLUSH_NONE) {
		/* Test if others are queued */
		if (msm_uport->rx.pending_flag & ~(1 << inx)) {
			MSM_HS_DBG("%s(): inx 0x%x, 0x%lx not processed\n",
			__func__, inx,
			msm_uport->rx.pending_flag & ~(1<<inx));
		}
		kthread_queue_work(&msm_uport->rx.kworker,
				&msm_uport->rx.kwork);
		MSM_HS_DBG("%s(): Scheduled rx_tlet\n", __func__);
	}
}

/*
 *  Standard API, Current states of modem control inputs
 *
 * Since CTS can be handled entirely by HARDWARE we always
 * indicate clear to send and count on the TX FIFO to block when
 * it fills up.
 *
 * - TIOCM_DCD
 * - TIOCM_CTS
 * - TIOCM_DSR
 * - TIOCM_RI
 *  (Unsupported) DCD and DSR will return them high. RI will return low.
 */
static unsigned int msm_hs_get_mctrl_locked(struct uart_port *uport)
{
	return TIOCM_DSR | TIOCM_CAR | TIOCM_CTS;
}

/*
 *  Standard API, Set or clear RFR_signal
 *
 * Set RFR high, (Indicate we are not ready for data), we disable auto
 * ready for receiving and then set RFR_N high. To set RFR to low we just turn
 * back auto ready for receiving and it should lower RFR signal
 * when hardware is ready
 */
void msm_hs_set_mctrl_locked(struct uart_port *uport,
				    unsigned int mctrl)
{
	unsigned int set_rts;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	if (msm_uport->pm_state != MSM_HS_PM_ACTIVE) {
		MSM_HS_WARN("%s(): Clocks are off\n", __func__);
		return;
	}
	/* RTS is active low */
	set_rts = TIOCM_RTS & mctrl ? 0 : 1;
	MSM_HS_INFO("%s(): set_rts %d\n", __func__, set_rts);

	if (set_rts)
		msm_hs_disable_flow_control(uport, false);
	else
		msm_hs_enable_flow_control(uport, false);
}

void msm_hs_set_mctrl(struct uart_port *uport,
				    unsigned int mctrl)
{
	unsigned long flags;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	msm_hs_resource_vote(msm_uport);
	spin_lock_irqsave(&uport->lock, flags);
	msm_hs_set_mctrl_locked(uport, mctrl);
	spin_unlock_irqrestore(&uport->lock, flags);
	msm_hs_resource_unvote(msm_uport);
}
EXPORT_SYMBOL_GPL(msm_hs_set_mctrl);

/* Standard API, Enable modem status (CTS) interrupt  */
static void msm_hs_enable_ms_locked(struct uart_port *uport)
{
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	if (msm_uport->pm_state != MSM_HS_PM_ACTIVE) {
		MSM_HS_WARN("%s(): Clocks are off\n", __func__);
		return;
	}

	/* Enable DELTA_CTS Interrupt */
	msm_uport->imr_reg |= UARTDM_ISR_DELTA_CTS_BMSK;
	msm_hs_write(uport, UART_DM_IMR, msm_uport->imr_reg);
	/* Ensure register IO completion */
	mb();

}

/*
 *  Standard API, Break Signal
 *
 * Control the transmission of a break signal. ctl eq 0 => break
 * signal terminate ctl ne 0 => start break signal
 */
static void msm_hs_break_ctl(struct uart_port *uport, int ctl)
{
	unsigned long flags;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	msm_hs_resource_vote(msm_uport);
	spin_lock_irqsave(&uport->lock, flags);
	msm_hs_write(uport, UART_DM_CR, ctl ? START_BREAK : STOP_BREAK);
	/* Ensure register IO completion */
	mb();
	spin_unlock_irqrestore(&uport->lock, flags);
	msm_hs_resource_unvote(msm_uport);
}

static void msm_hs_config_port(struct uart_port *uport, int cfg_flags)
{
	if (cfg_flags & UART_CONFIG_TYPE)
		uport->type = PORT_MSM;

}

/*  Handle CTS changes (Called from interrupt handler) */
static void msm_hs_handle_delta_cts_locked(struct uart_port *uport)
{
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	msm_hs_resource_vote(msm_uport);
	/* clear interrupt */
	msm_hs_write(uport, UART_DM_CR, RESET_CTS);
	/* Calling CLOCK API. Hence mb() requires here. */
	mb();
	uport->icount.cts++;

	/* clear the IOCTL TIOCMIWAIT if called */
	wake_up_interruptible(&uport->state->port.delta_msr_wait);
	msm_hs_resource_unvote(msm_uport);
}

static irqreturn_t msm_hs_isr(int irq, void *dev)
{
	unsigned long flags;
	unsigned int isr_status;
	struct msm_hs_port *msm_uport = (struct msm_hs_port *)dev;
	struct uart_port *uport = &msm_uport->uport;
	struct circ_buf *tx_buf = &uport->state->xmit;
	struct msm_hs_tx *tx = &msm_uport->tx;

	spin_lock_irqsave(&uport->lock, flags);

	isr_status = msm_hs_read(uport, UART_DM_MISR);
	MSM_HS_INFO("%s(): DM_ISR: 0x%x\n", __func__, isr_status);
	dump_uart_hs_registers(msm_uport);

	/* Uart RX starting */
	if (isr_status & UARTDM_ISR_RXLEV_BMSK) {
		MSM_HS_DBG("%s():UARTDM_ISR_RXLEV_BMSK\n", __func__);
		msm_uport->imr_reg &= ~UARTDM_ISR_RXLEV_BMSK;
		msm_hs_write(uport, UART_DM_IMR, msm_uport->imr_reg);
		/* Complete device write for IMR. Hence mb() requires. */
		mb();
	}
	/* Stale rx interrupt */
	if (isr_status & UARTDM_ISR_RXSTALE_BMSK) {
		msm_hs_write(uport, UART_DM_CR, STALE_EVENT_DISABLE);
		msm_hs_write(uport, UART_DM_CR, RESET_STALE_INT);
		/*
		 * Complete device write before calling DMOV API. Hence
		 * mb() requires here.
		 */
		mb();
		MSM_HS_DBG("%s():Stal Interrupt\n", __func__);
	}
	/* tx ready interrupt */
	if (isr_status & UARTDM_ISR_TX_READY_BMSK) {
		MSM_HS_DBG("%s(): ISR_TX_READY Interrupt\n", __func__);
		/* Clear  TX Ready */
		msm_hs_write(uport, UART_DM_CR, CLEAR_TX_READY);

		/*
		 * Complete both writes before starting new TX.
		 * Hence mb() requires here.
		 */
		mb();
		/* Complete DMA TX transactions and submit new transactions */

		/* Do not update tx_buf.tail if uart_flush_buffer already
		 * called in serial core
		 */
		if (!uart_circ_empty(tx_buf))
			tx_buf->tail = (tx_buf->tail +
					tx->tx_count) & ~UART_XMIT_SIZE;

		tx->dma_in_flight = false;

		uport->icount.tx += tx->tx_count;

		if (uart_circ_chars_pending(tx_buf) < WAKEUP_CHARS)
			uart_write_wakeup(uport);
	}
	if (isr_status & UARTDM_ISR_TXLEV_BMSK) {
		/* TX FIFO is empty */
		msm_uport->imr_reg &= ~UARTDM_ISR_TXLEV_BMSK;
		msm_hs_write(uport, UART_DM_IMR, msm_uport->imr_reg);
		MSM_HS_DBG("%s(): TXLEV Interrupt\n", __func__);
		/*
		 * Complete device write before starting clock_off request.
		 * Hence mb() requires here.
		 */
		mb();
		queue_work(msm_uport->hsuart_wq, &msm_uport->clock_off_w);
	}

	/* Change in CTS interrupt */
	if (isr_status & UARTDM_ISR_DELTA_CTS_BMSK)
		msm_hs_handle_delta_cts_locked(uport);

	spin_unlock_irqrestore(&uport->lock, flags);

	return IRQ_HANDLED;
}

/* The following two functions provide interfaces to get the underlying
 * port structure (struct uart_port or struct msm_hs_port) given
 * the port index. msm_hs_get_uart port is called by clients.
 * The function msm_hs_get_hs_port is for internal use
 */

struct uart_port *msm_hs_get_uart_port(int port_index)
{
	struct uart_state *state = msm_hs_driver.state + port_index;

	/* The uart_driver structure stores the states in an array.
	 * Thus the corresponding offset from the drv->state returns
	 * the state for the uart_port that is requested
	 */
	if (port_index == state->uart_port->line)
		return state->uart_port;

	return NULL;
}
EXPORT_SYMBOL_GPL(msm_hs_get_uart_port);

static struct msm_hs_port *msm_hs_get_hs_port(int port_index)
{
	struct uart_port *uport = msm_hs_get_uart_port(port_index);

	if (uport)
		return UARTDM_TO_MSM(uport);
	return NULL;
}

void enable_wakeup_interrupt(struct msm_hs_port *msm_uport)
{
	unsigned long flags;
	int ret;
	struct uart_port *uport = &(msm_uport->uport);

	if (!is_use_low_power_wakeup(msm_uport))
		return;

	if (!(msm_uport->wakeup.enabled)) {
		spin_lock_irqsave(&uport->lock, flags);
		msm_uport->wakeup.ignore = 1;
		msm_uport->wakeup.enabled = true;
		spin_unlock_irqrestore(&uport->lock, flags);
		disable_irq(uport->irq);
		enable_irq(msm_uport->wakeup.irq);
		ret = irq_set_irq_wake(msm_uport->wakeup.irq, 1);
		if (unlikely(ret))
			MSM_HS_WARN("%s:Failed to set IRQ wake:%d\n", __func__, ret);
	} else {
		MSM_HS_WARN("%s():Wake up IRQ already enabled\n", __func__);
	}
}

void disable_wakeup_interrupt(struct msm_hs_port *msm_uport)
{
	unsigned long flags;
	int ret;
	struct uart_port *uport = &(msm_uport->uport);

	if (!is_use_low_power_wakeup(msm_uport))
		return;

	if (msm_uport->wakeup.enabled) {
		ret = irq_set_irq_wake(msm_uport->wakeup.irq, 0);
		if (unlikely(ret))
			MSM_HS_WARN("%s:Failed to unset IRQ wake:%d\n", __func__, ret);
		disable_irq_nosync(msm_uport->wakeup.irq);
		enable_irq(uport->irq);
		spin_lock_irqsave(&uport->lock, flags);
		msm_uport->wakeup.enabled = false;
		spin_unlock_irqrestore(&uport->lock, flags);
	} else {
		MSM_HS_WARN("%s():Wake up IRQ already disabled\n", __func__);
	}
}

void msm_hs_resource_off(struct msm_hs_port *msm_uport)
{
	struct uart_port *uport = &(msm_uport->uport);
	unsigned int data;

	if (pinctrl_select_state(msm_uport->pinctrl,
				 msm_uport->gpio_state_suspend))
		MSM_HS_ERR("%s():Error selecting pinctrl suspend state\n", __func__);

	msm_hs_disable_flow_control(uport, false);
	if (msm_uport->rx.flush == FLUSH_NONE)
		msm_hs_disconnect_rx(uport);

	/* disable dlink */
	if (msm_uport->tx.flush == FLUSH_NONE)
		wait_event_timeout(msm_uport->tx.wait,
			msm_uport->tx.flush == FLUSH_STOP, 500);

	if (msm_uport->tx.flush != FLUSH_SHUTDOWN) {
		data = msm_hs_read(uport, UART_DM_DMEN);
		data &= ~UARTDM_TX_BAM_ENABLE_BMSK;
		msm_hs_write(uport, UART_DM_DMEN, data);
		sps_tx_disconnect(msm_uport);
	}
	if (!atomic_read(&msm_uport->client_req_state))
		msm_hs_enable_flow_control(uport, false);
}

void msm_hs_resource_on(struct msm_hs_port *msm_uport)
{
	struct uart_port *uport = &(msm_uport->uport);
	unsigned int data;
	unsigned long flags;

	if (pinctrl_select_state(msm_uport->pinctrl,
				 msm_uport->gpio_state_active))
		MSM_HS_ERR("%s():Error selecting active state\n", __func__);

	if (msm_uport->rx.flush == FLUSH_SHUTDOWN ||
	msm_uport->rx.flush == FLUSH_STOP) {
		msm_hs_write(uport, UART_DM_CR, RESET_RX);
		data = msm_hs_read(uport, UART_DM_DMEN);
		data |= UARTDM_RX_BAM_ENABLE_BMSK;
		msm_hs_write(uport, UART_DM_DMEN, data);
	}

	msm_hs_spsconnect_tx(msm_uport);
	if (msm_uport->rx.flush == FLUSH_SHUTDOWN) {
		msm_hs_spsconnect_rx(uport);
		spin_lock_irqsave(&uport->lock, flags);
		msm_hs_start_rx_locked(uport);
		spin_unlock_irqrestore(&uport->lock, flags);
	}
}

/* Request to turn off uart clock once pending TX is flushed */
int msm_hs_request_clock_off(struct uart_port *uport)
{
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);
	int ret = 0;
	int client_count = 0;

	mutex_lock(&msm_uport->mtx);
	/*
	 * If we're in the middle of a system suspend, don't process these
	 * userspace/kernel API commands.
	 */
	if (msm_uport->pm_state == MSM_HS_PM_SYS_SUSPENDED) {
		MSM_HS_WARN("%s():Can't process clk request during suspend\n",
			__func__);
		ret = -EIO;
	}
	mutex_unlock(&msm_uport->mtx);
	if (ret)
		goto exit_request_clock_off;

	if (atomic_read(&msm_uport->client_count) <= 0) {
		MSM_HS_WARN("%s(): ioctl count -ve, client check voting\n",
			__func__);
		ret = -EPERM;
		goto exit_request_clock_off;
	}
	/* Set the flag to disable flow control and wakeup irq */
	if (msm_uport->obs)
		atomic_set(&msm_uport->client_req_state, 1);
	msm_hs_resource_unvote(msm_uport);
	atomic_dec(&msm_uport->client_count);
	client_count = atomic_read(&msm_uport->client_count);
	LOG_USR_MSG(msm_uport->ipc_msm_hs_pwr_ctxt,
			"%s(): Client_Count %d\n", __func__,
			client_count);
exit_request_clock_off:
	return ret;
}
EXPORT_SYMBOL_GPL(msm_hs_request_clock_off);

int msm_hs_request_clock_on(struct uart_port *uport)
{
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);
	int client_count;
	int ret = 0;

	mutex_lock(&msm_uport->mtx);
	/*
	 * If we're in the middle of a system suspend, don't process these
	 * userspace/kernel API commands.
	 */
	if (msm_uport->pm_state == MSM_HS_PM_SYS_SUSPENDED) {
		MSM_HS_WARN("%s(): Can't process clk request during suspend\n",
			__func__);
		ret = -EIO;
	}
	mutex_unlock(&msm_uport->mtx);
	if (ret)
		goto exit_request_clock_on;

	msm_hs_resource_vote(UARTDM_TO_MSM(uport));
	atomic_inc(&msm_uport->client_count);
	client_count = atomic_read(&msm_uport->client_count);
	LOG_USR_MSG(msm_uport->ipc_msm_hs_pwr_ctxt,
			"%s(): Client_Count %d\n", __func__,
			client_count);

	/* Clear the flag */
	if (msm_uport->obs)
		atomic_set(&msm_uport->client_req_state, 0);
exit_request_clock_on:
	return ret;
}
EXPORT_SYMBOL_GPL(msm_hs_request_clock_on);

static irqreturn_t msm_hs_wakeup_isr(int irq, void *dev)
{
	unsigned int wakeup = 0;
	unsigned long flags;
	struct msm_hs_port *msm_uport = (struct msm_hs_port *)dev;
	struct uart_port *uport = &msm_uport->uport;
	struct tty_struct *tty = NULL;

	spin_lock_irqsave(&uport->lock, flags);

	if (msm_uport->wakeup.ignore)
		msm_uport->wakeup.ignore = 0;
	else
		wakeup = 1;

	if (wakeup) {
		/*
		 * Port was clocked off during rx, wake up and
		 * optionally inject char into tty rx
		 */
		if (msm_uport->wakeup.inject_rx) {
			tty = uport->state->port.tty;
			tty_insert_flip_char(tty->port,
					     msm_uport->wakeup.rx_to_inject,
					     TTY_NORMAL);
			hex_dump_ipc(msm_uport, msm_uport->rx.ipc_rx_ctxt,
				"Rx Inject",
				&msm_uport->wakeup.rx_to_inject, 0, 1);
			MSM_HS_INFO("Wakeup ISR.Ignore%d\n",
						msm_uport->wakeup.ignore);
		}
	}

	spin_unlock_irqrestore(&uport->lock, flags);

	if (wakeup && msm_uport->wakeup.inject_rx)
		tty_flip_buffer_push(tty->port);
	return IRQ_HANDLED;
}

static const char *msm_hs_type(struct uart_port *port)
{
	return "MSM HS UART";
}

/**
 * msm_hs_unconfig_uart_gpios: Unconfigures UART GPIOs
 * @uport: uart port
 */
static void msm_hs_unconfig_uart_gpios(struct uart_port *uport)
{
	struct platform_device *pdev = to_platform_device(uport->dev);
	const struct msm_serial_hs_platform_data *pdata =
					pdev->dev.platform_data;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);
	int ret;

	if (msm_uport->use_pinctrl) {
		ret = pinctrl_select_state(msm_uport->pinctrl,
				msm_uport->gpio_state_suspend);
		if (ret)
			MSM_HS_ERR("%s():Failed to pinctrl set_state\n",
				__func__);
	} else if (pdata) {
		if (gpio_is_valid(pdata->uart_tx_gpio))
			gpio_free(pdata->uart_tx_gpio);
		if (gpio_is_valid(pdata->uart_rx_gpio))
			gpio_free(pdata->uart_rx_gpio);
		if (gpio_is_valid(pdata->uart_cts_gpio))
			gpio_free(pdata->uart_cts_gpio);
		if (gpio_is_valid(pdata->uart_rfr_gpio))
			gpio_free(pdata->uart_rfr_gpio);
	} else
		MSM_HS_ERR("%s(): Error:Pdata is NULL\n", __func__);
}

/**
 * msm_hs_config_uart_gpios - Configures UART GPIOs
 * @uport: uart port
 */
static int msm_hs_config_uart_gpios(struct uart_port *uport)
{
	struct platform_device *pdev = to_platform_device(uport->dev);
	const struct msm_serial_hs_platform_data *pdata =
					pdev->dev.platform_data;
	int ret = 0;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	if (!IS_ERR_OR_NULL(msm_uport->pinctrl)) {
		MSM_HS_DBG("%s(): Using Pinctrl\n", __func__);
		msm_uport->use_pinctrl = true;
		ret = pinctrl_select_state(msm_uport->pinctrl,
				msm_uport->gpio_state_active);
		if (ret)
			MSM_HS_ERR("%s(): Failed to pinctrl set_state\n",
				__func__);
		return ret;
	} else if (pdata) {
		/* Fall back to using gpio lib */
		if (gpio_is_valid(pdata->uart_tx_gpio)) {
			ret = gpio_request(pdata->uart_tx_gpio,
							"UART_TX_GPIO");
			if (unlikely(ret)) {
				MSM_HS_ERR("gpio request failed for:%d\n",
					pdata->uart_tx_gpio);
				goto exit_uart_config;
			}
		}

		if (gpio_is_valid(pdata->uart_rx_gpio)) {
			ret = gpio_request(pdata->uart_rx_gpio,
							"UART_RX_GPIO");
			if (unlikely(ret)) {
				MSM_HS_ERR("gpio request failed for:%d\n",
					pdata->uart_rx_gpio);
				goto uart_tx_unconfig;
			}
		}

		if (gpio_is_valid(pdata->uart_cts_gpio)) {
			ret = gpio_request(pdata->uart_cts_gpio,
							"UART_CTS_GPIO");
			if (unlikely(ret)) {
				MSM_HS_ERR("gpio request failed for:%d\n",
					pdata->uart_cts_gpio);
				goto uart_rx_unconfig;
			}
		}

		if (gpio_is_valid(pdata->uart_rfr_gpio)) {
			ret = gpio_request(pdata->uart_rfr_gpio,
							"UART_RFR_GPIO");
			if (unlikely(ret)) {
				MSM_HS_ERR("gpio request failed for:%d\n",
					pdata->uart_rfr_gpio);
				goto uart_cts_unconfig;
			}
		}
	} else {
		MSM_HS_ERR("%s(): Pdata is NULL\n", __func__);
		ret = -EINVAL;
	}
	return ret;

uart_cts_unconfig:
	if (gpio_is_valid(pdata->uart_cts_gpio))
		gpio_free(pdata->uart_cts_gpio);
uart_rx_unconfig:
	if (gpio_is_valid(pdata->uart_rx_gpio))
		gpio_free(pdata->uart_rx_gpio);
uart_tx_unconfig:
	if (gpio_is_valid(pdata->uart_tx_gpio))
		gpio_free(pdata->uart_tx_gpio);
exit_uart_config:
	return ret;
}


static void msm_hs_get_pinctrl_configs(struct uart_port *uport)
{
	struct pinctrl_state *set_state;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	msm_uport->pinctrl = devm_pinctrl_get(uport->dev);
	if (IS_ERR_OR_NULL(msm_uport->pinctrl)) {
		MSM_HS_DBG("%s(): Pinctrl not defined\n", __func__);
	} else {
		MSM_HS_DBG("%s(): Using Pinctrl\n", __func__);
		msm_uport->use_pinctrl = true;

		set_state = pinctrl_lookup_state(msm_uport->pinctrl, PINCTRL_STATE_ACTIVE);
		if (IS_ERR_OR_NULL(set_state)) {
			set_state = pinctrl_lookup_state(msm_uport->pinctrl, PINCTRL_STATE_DEFAULT);
			if (IS_ERR_OR_NULL(set_state)) {
				dev_err(uport->dev, "pinctrl lookup failed for default state\n");
				goto pinctrl_fail;
			}
			MSM_HS_DBG("%s(): Pinctrl state default %pK\n", __func__, set_state);
		} else {
			MSM_HS_DBG("%s(): Pinctrl state active %pK\n", __func__, set_state);
		}
		msm_uport->gpio_state_active = set_state;

		set_state = pinctrl_lookup_state(msm_uport->pinctrl,
						PINCTRL_STATE_SLEEP);
		if (IS_ERR_OR_NULL(set_state)) {
			dev_err(uport->dev,
				"pinctrl lookup failed for sleep state\n");
			goto pinctrl_fail;
		}

		MSM_HS_DBG("%s(): Pinctrl state sleep %pK\n", __func__,
			set_state);
		msm_uport->gpio_state_suspend = set_state;

		set_state = pinctrl_lookup_state(msm_uport->pinctrl, PINCTRL_STATE_SHUTDOWN);
		if (IS_ERR_OR_NULL(set_state)) {
			dev_err(uport->dev, "pinctrl lookup failed for shutdown state\n");
			goto pinctrl_fail;
		}

		MSM_HS_DBG("%s(): Pinctrl state shutdown %pK\n", __func__, set_state);
		msm_uport->gpio_state_shutdown = set_state;

		return;
	}
pinctrl_fail:
	msm_uport->pinctrl = NULL;
}

/* Called when port is opened */
static int msm_hs_startup(struct uart_port *uport)
{
	int ret;
	int rfr_level;
	unsigned long flags;
	unsigned int data;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);
	struct circ_buf *tx_buf = &uport->state->xmit;
	struct msm_hs_tx *tx = &msm_uport->tx;
	struct msm_hs_rx *rx = &msm_uport->rx;
	struct sps_pipe *sps_pipe_handle_tx = tx->cons.pipe_handle;

	rfr_level = uport->fifosize;
	if (rfr_level > 16)
		rfr_level -= 16;

	tx->dma_base = dma_map_single(uport->dev, tx_buf->buf, UART_XMIT_SIZE,
				      DMA_TO_DEVICE);

	/* turn on uart clk */
	msm_hs_resource_vote(msm_uport);

	ret = msm_hs_config_uart_gpios(uport);
	if (ret) {
		MSM_HS_ERR("%s(): Uart GPIO request failed\n", __func__);
		goto unvote_exit;
	}

	msm_hs_write(uport, UART_DM_DMEN, 0);

	/* Connect TX */
	sps_tx_disconnect(msm_uport);
	ret = msm_hs_spsconnect_tx(msm_uport);
	if (ret) {
		MSM_HS_ERR("msm_serial_hs: SPS connect failed for TX\n");
		goto unconfig_uart_gpios;
	}

	/* Connect RX */
	kthread_flush_worker(&msm_uport->rx.kworker);
	if (rx->flush != FLUSH_SHUTDOWN)
		disconnect_rx_endpoint(msm_uport);
	ret = msm_hs_spsconnect_rx(uport);
	if (ret) {
		MSM_HS_ERR("msm_serial_hs: SPS connect failed for RX\n");
		goto sps_disconnect_tx;
	}

	data = (UARTDM_BCR_TX_BREAK_DISABLE | UARTDM_BCR_STALE_IRQ_EMPTY |
		UARTDM_BCR_RX_DMRX_LOW_EN | UARTDM_BCR_RX_STAL_IRQ_DMRX_EQL |
		UARTDM_BCR_RX_DMRX_1BYTE_RES_EN);
	msm_hs_write(uport, UART_DM_BCR, data);

	/* Set auto RFR Level */
	data = msm_hs_read(uport, UART_DM_MR1);
	data &= ~UARTDM_MR1_AUTO_RFR_LEVEL1_BMSK;
	data &= ~UARTDM_MR1_AUTO_RFR_LEVEL0_BMSK;
	data |= (UARTDM_MR1_AUTO_RFR_LEVEL1_BMSK & (rfr_level << 2));
	data |= (UARTDM_MR1_AUTO_RFR_LEVEL0_BMSK & rfr_level);
	msm_hs_write(uport, UART_DM_MR1, data);

	/* Make sure RXSTALE count is non-zero */
	data = msm_hs_read(uport, UART_DM_IPR);
	if (!data) {
		data |= 0x1f & UARTDM_IPR_STALE_LSB_BMSK;
		msm_hs_write(uport, UART_DM_IPR, data);
	}

	/* Assume no flow control, unless termios sets it */
	msm_uport->flow_control = false;
	msm_hs_disable_flow_control(uport, true);


	/* Reset TX */
	msm_hs_write(uport, UART_DM_CR, RESET_TX);
	msm_hs_write(uport, UART_DM_CR, RESET_RX);
	msm_hs_write(uport, UART_DM_CR, RESET_ERROR_STATUS);
	msm_hs_write(uport, UART_DM_CR, RESET_BREAK_INT);
	msm_hs_write(uport, UART_DM_CR, RESET_STALE_INT);
	msm_hs_write(uport, UART_DM_CR, RESET_CTS);
	msm_hs_write(uport, UART_DM_CR, RFR_LOW);
	/* Turn on Uart Receiver */
	msm_hs_write(uport, UART_DM_CR, UARTDM_CR_RX_EN_BMSK);

	/* Turn on Uart Transmitter */
	msm_hs_write(uport, UART_DM_CR, UARTDM_CR_TX_EN_BMSK);

	tx->dma_in_flight = false;
	MSM_HS_DBG("%s():desc usage flag 0x%lx\n", __func__, rx->queued_flag);
	timer_setup(&(tx->tx_timeout_timer),
			tx_timeout_handler, TIMER_IRQSAFE);

	/* Enable reading the current CTS, no harm even if CTS is ignored */
	msm_uport->imr_reg |= UARTDM_ISR_CURRENT_CTS_BMSK;

	/* TXLEV on empty TX fifo */
	msm_hs_write(uport, UART_DM_TFWR, 4);
	/*
	 * Complete all device write related configuration before
	 * queuing RX request. Hence mb() requires here.
	 */
	mb();

	spin_lock_irqsave(&uport->lock, flags);
	atomic_set(&msm_uport->client_count, 0);
	atomic_set(&msm_uport->client_req_state, 0);
	LOG_USR_MSG(msm_uport->ipc_msm_hs_pwr_ctxt,
			"%s(): Client_Count 0\n", __func__);
	msm_hs_start_rx_locked(uport);

	spin_unlock_irqrestore(&uport->lock, flags);

	msm_hs_resource_unvote(msm_uport);
	return 0;

sps_disconnect_tx:
	sps_disconnect(sps_pipe_handle_tx);
unconfig_uart_gpios:
	msm_hs_unconfig_uart_gpios(uport);
unvote_exit:
	msm_hs_resource_unvote(msm_uport);

	MSM_HS_ERR("%s(): Error return\n", __func__);
	return ret;
}

/* Initialize tx and rx data structures */
static int uartdm_init_port(struct uart_port *uport)
{
	int ret = 0;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);
	struct msm_hs_tx *tx = &msm_uport->tx;
	struct msm_hs_rx *rx = &msm_uport->rx;

	init_waitqueue_head(&rx->wait);
	init_waitqueue_head(&tx->wait);
	init_waitqueue_head(&msm_uport->bam_disconnect_wait);

	/* Init kernel threads for tx and rx */

	kthread_init_worker(&rx->kworker);
	rx->task = kthread_run(kthread_worker_fn,
			&rx->kworker, "msm_serial_hs_%d_rx_work", uport->line);
	if (IS_ERR(rx->task)) {
		MSM_HS_ERR("%s(): error creating task\n", __func__);
		goto exit_lh_init;
	}
	kthread_init_work(&rx->kwork, msm_serial_hs_rx_work);

	kthread_init_worker(&tx->kworker);
	tx->task = kthread_run(kthread_worker_fn,
			&tx->kworker, "msm_serial_hs_%d_tx_work", uport->line);
	if (IS_ERR(rx->task)) {
		MSM_HS_ERR("%s(): error creating task\n", __func__);
		goto exit_lh_init;
	}

	kthread_init_work(&tx->kwork, msm_serial_hs_tx_work);

	rx->buffer = dma_alloc_coherent(uport->dev,
				UART_DMA_DESC_NR * UARTDM_RX_BUF_SIZE,
				 &rx->rbuffer, GFP_KERNEL);
	if (!rx->buffer) {
		MSM_HS_ERR("%s(): cannot allocate rx->buffer\n", __func__);
		ret = -ENOMEM;
		goto exit_lh_init;
	}

	/* Set up Uart Receive */
	msm_hs_write(uport, UART_DM_RFWR, 32);
	/* Write to BADR explicitly to set up FIFO sizes */
	msm_hs_write(uport, UARTDM_BADR_ADDR, 64);

	INIT_DELAYED_WORK(&rx->flip_insert_work, flip_insert_work);

	return ret;
exit_lh_init:
	kthread_stop(rx->task);
	rx->task = NULL;
	kthread_stop(tx->task);
	tx->task = NULL;
	return ret;
}

struct msm_serial_hs_platform_data
	*msm_hs_dt_to_pdata(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct msm_serial_hs_platform_data *pdata;
	u32 rx_to_inject;
	int ret;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	pdev->id = of_alias_get_id(pdev->dev.of_node, "uart");
	/* UART TX GPIO */
	pdata->uart_tx_gpio = of_get_named_gpio(node,
					"qcom,tx-gpio", 0);
	if (pdata->uart_tx_gpio < 0)
		pr_err("uart_tx_gpio is not available\n");

	/* UART RX GPIO */
	pdata->uart_rx_gpio = of_get_named_gpio(node,
					"qcom,rx-gpio", 0);
	if (pdata->uart_rx_gpio < 0)
		pr_err("uart_rx_gpio is not available\n");

	/* UART CTS GPIO */
	pdata->uart_cts_gpio = of_get_named_gpio(node,
					"qcom,cts-gpio", 0);
	if (pdata->uart_cts_gpio < 0)
		pr_err("uart_cts_gpio is not available\n");

	/* UART RFR GPIO */
	pdata->uart_rfr_gpio = of_get_named_gpio(node,
					"qcom,rfr-gpio", 0);
	if (pdata->uart_rfr_gpio < 0)
		pr_err("uart_rfr_gpio is not available\n");

	pdata->no_suspend_delay = of_property_read_bool(node,
				"qcom,no-suspend-delay");

	pdata->obs = of_property_read_bool(node,
				"qcom,msm-obs");
	if (pdata->obs)
		pr_err("%s():Out of Band sleep flag is set\n", __func__);

	pdata->inject_rx_on_wakeup = of_property_read_bool(node,
				"qcom,inject-rx-on-wakeup");

	if (pdata->inject_rx_on_wakeup) {
		ret = of_property_read_u32(node, "qcom,rx-char-to-inject",
						&rx_to_inject);
		if (ret < 0) {
			pr_err("Error: Rx_char_to_inject not specified\n");
			return ERR_PTR(ret);
		}
		pdata->rx_to_inject = (u8)rx_to_inject;
	}

	ret = of_property_read_u32(node, "qcom,bam-tx-ep-pipe-index",
				&pdata->bam_tx_ep_pipe_index);
	if (ret < 0) {
		pr_err("Error: Getting UART BAM TX EP Pipe Index\n");
		return ERR_PTR(ret);
	}

	if (!(pdata->bam_tx_ep_pipe_index >= BAM_PIPE_MIN &&
		pdata->bam_tx_ep_pipe_index <= BAM_PIPE_MAX)) {
		pr_err("Error: Invalid UART BAM TX EP Pipe Index\n");
		return ERR_PTR(-EINVAL);
	}

	ret = of_property_read_u32(node, "qcom,bam-rx-ep-pipe-index",
					&pdata->bam_rx_ep_pipe_index);
	if (ret < 0) {
		pr_err("Error: Getting UART BAM RX EP Pipe Index\n");
		return ERR_PTR(ret);
	}

	if (!(pdata->bam_rx_ep_pipe_index >= BAM_PIPE_MIN &&
		pdata->bam_rx_ep_pipe_index <= BAM_PIPE_MAX)) {
		pr_err("Error: Invalid UART BAM RX EP Pipe Index\n");
		return ERR_PTR(-EINVAL);
	}

	pr_debug("tx_ep_pipe_index:%d rx_ep_pipe_index:%d\n"
		"tx_gpio:%d rx_gpio:%d rfr_gpio:%d cts_gpio:%d\n",
		pdata->bam_tx_ep_pipe_index, pdata->bam_rx_ep_pipe_index,
		pdata->uart_tx_gpio, pdata->uart_rx_gpio, pdata->uart_cts_gpio,
		pdata->uart_rfr_gpio);

	return pdata;
}


/**
 * Deallocate UART peripheral's SPS endpoint
 * @msm_uport - Pointer to msm_hs_port structure
 * @ep - Pointer to sps endpoint data structure
 */

static void msm_hs_exit_ep_conn(struct msm_hs_port *msm_uport,
				struct msm_hs_sps_ep_conn_data *ep)
{
	struct sps_pipe *sps_pipe_handle = ep->pipe_handle;
	struct sps_connect *sps_config = &ep->config;

	dma_free_coherent(msm_uport->uport.dev,
			sps_config->desc.size,
			&sps_config->desc.phys_base,
			GFP_KERNEL);
	sps_free_endpoint(sps_pipe_handle);
}


/**
 * Allocate UART peripheral's SPS endpoint
 *
 * This function allocates endpoint context
 * by calling appropriate SPS driver APIs.
 *
 * @msm_uport - Pointer to msm_hs_port structure
 * @ep - Pointer to sps endpoint data structure
 * @is_produce - 1 means Producer endpoint
 *             - 0 means Consumer endpoint
 *
 * @return - 0 if successful else negative value
 */

static int msm_hs_sps_init_ep_conn(struct msm_hs_port *msm_uport,
				struct msm_hs_sps_ep_conn_data *ep,
				bool is_producer)
{
	int rc = 0;
	struct sps_pipe *sps_pipe_handle;
	struct sps_connect *sps_config = &ep->config;
	struct sps_register_event *sps_event = &ep->event;

	/* Allocate endpoint context */
	sps_pipe_handle = sps_alloc_endpoint();
	if (!sps_pipe_handle) {
		MSM_HS_ERR("%s(): sps_alloc_endpoint, failed,is_producer=%d\n",
			 __func__, is_producer);
		rc = -ENOMEM;
		goto out;
	}

	/* Get default connection configuration for an endpoint */
	rc = sps_get_config(sps_pipe_handle, sps_config);
	if (rc) {
		MSM_HS_ERR("%s(): failed,pipe_handle=0x%pK rc=%d\n",
			__func__, sps_pipe_handle, rc);
		goto get_config_err;
	}

	/* Modify the default connection configuration */
	if (is_producer) {
		/* For UART producer transfer, source is UART peripheral
		 * where as destination is system memory
		 */
		sps_config->source = msm_uport->bam_handle;
		sps_config->destination = SPS_DEV_HANDLE_MEM;
		sps_config->mode = SPS_MODE_SRC;
		sps_config->src_pipe_index = msm_uport->bam_rx_ep_pipe_index;
		sps_config->dest_pipe_index = 0;
		sps_event->callback = msm_hs_sps_rx_callback;
	} else {
		/* For UART consumer transfer, source is system memory
		 * where as destination is UART peripheral
		 */
		sps_config->source = SPS_DEV_HANDLE_MEM;
		sps_config->destination = msm_uport->bam_handle;
		sps_config->mode = SPS_MODE_DEST;
		sps_config->src_pipe_index = 0;
		sps_config->dest_pipe_index = msm_uport->bam_tx_ep_pipe_index;
		sps_event->callback = msm_hs_sps_tx_callback;
	}

	sps_config->options = SPS_O_EOT | SPS_O_DESC_DONE | SPS_O_AUTO_ENABLE;
	sps_config->event_thresh = 0x10;

	/* Allocate maximum descriptor fifo size */
	sps_config->desc.size =
		(1 + UART_DMA_DESC_NR) * sizeof(struct sps_iovec);
	sps_config->desc.base = dma_alloc_coherent(msm_uport->uport.dev,
						sps_config->desc.size,
						&sps_config->desc.phys_base,
						GFP_KERNEL);
	if (!sps_config->desc.base) {
		rc = -ENOMEM;
		MSM_HS_ERR("msm_serial_hs: dma_alloc_coherent() failed\n");
		goto get_config_err;
	}

	sps_event->mode = SPS_TRIGGER_CALLBACK;

	sps_event->options = SPS_O_DESC_DONE | SPS_O_EOT;
	sps_event->user = (void *)msm_uport;

	/* Now save the sps pipe handle */
	ep->pipe_handle = sps_pipe_handle;
	MSM_HS_DBG("%s(): %s: pipe_handle=0x%pK, desc_phys_base=0x%pa\n",
		 __func__, is_producer ? "READ" : "WRITE",
		sps_pipe_handle, &sps_config->desc.phys_base);
	return 0;

get_config_err:
	sps_free_endpoint(sps_pipe_handle);
out:
	return rc;
}

/**
 * Initialize SPS HW connected with UART core
 *
 * This function register BAM HW resources with
 * SPS driver and then initialize 2 SPS endpoints
 *
 * msm_uport - Pointer to msm_hs_port structure
 *
 * @return - 0 if successful else negative value
 */

static int msm_hs_sps_init(struct msm_hs_port *msm_uport)
{
	int rc = 0;
	struct sps_bam_props bam = {0};
	unsigned long bam_handle;

	rc = sps_phy2h(msm_uport->bam_mem, &bam_handle);
	if (rc || !bam_handle) {
		bam.phys_addr = msm_uport->bam_mem;
		bam.virt_addr = msm_uport->bam_base;
		/*
		 * This event threshold is only significant for BAM-to-BAM
		 * transfer. It's ignored for BAM-to-System mode transfer.
		 */
		bam.event_threshold = 0x10;	/* Pipe event threshold */
		bam.summing_threshold = 1;	/* BAM event threshold */

		/* SPS driver wll handle the UART BAM IRQ */
		bam.irq = (u32)msm_uport->bam_irq;
		bam.manage = SPS_BAM_MGR_DEVICE_REMOTE;

		MSM_HS_DBG("msm_serial_hs: bam physical base=0x%pa\n",
							&bam.phys_addr);
		MSM_HS_DBG("msm_serial_hs: bam virtual base=0x%pa\n",
							bam.virt_addr);

		/* Register UART Peripheral BAM device to SPS driver */
		rc = sps_register_bam_device(&bam, &bam_handle);
		if (rc) {
			MSM_HS_ERR("%s(): BAM device register failed\n",
				  __func__);
			return rc;
		}
		MSM_HS_DBG("%s():BAM device registered. bam_handle=0x%lx\n",
			   __func__, msm_uport->bam_handle);
	}
	msm_uport->bam_handle = bam_handle;

	rc = msm_hs_sps_init_ep_conn(msm_uport, &msm_uport->rx.prod,
				UART_SPS_PROD_PERIPHERAL);
	if (rc) {
		MSM_HS_ERR("%s(): Failed to Init Producer BAM-pipe\n",
				__func__);
		goto deregister_bam;
	}

	rc = msm_hs_sps_init_ep_conn(msm_uport, &msm_uport->tx.cons,
				UART_SPS_CONS_PERIPHERAL);
	if (rc) {
		MSM_HS_ERR("%s(): Failed to Init Consumer BAM-pipe\n",
				__func__);
		goto deinit_ep_conn_prod;
	}
	return 0;

deinit_ep_conn_prod:
	msm_hs_exit_ep_conn(msm_uport, &msm_uport->rx.prod);
deregister_bam:
	sps_deregister_bam_device(msm_uport->bam_handle);
	return rc;
}


static bool deviceid[UARTDM_NR] = {0};
/*
 * The mutex synchronizes grabbing next free device number
 * both in case of an alias being used or not. When alias is
 * used, the msm_hs_dt_to_pdata gets it and the boolean array
 * is accordingly updated with device_id_set_used. If no alias
 * is used, then device_id_grab_next_free sets that array.
 */
static DEFINE_MUTEX(mutex_next_device_id);

static int device_id_grab_next_free(void)
{
	int i;
	int ret = -ENODEV;

	mutex_lock(&mutex_next_device_id);
	for (i = 0; i < UARTDM_NR; i++)
		if (!deviceid[i]) {
			ret = i;
			deviceid[i] = true;
			break;
		}
	mutex_unlock(&mutex_next_device_id);
	return ret;
}

static int device_id_set_used(int index)
{
	int ret = 0;

	mutex_lock(&mutex_next_device_id);
	if (deviceid[index])
		ret = -ENODEV;
	else
		deviceid[index] = true;
	mutex_unlock(&mutex_next_device_id);
	return ret;
}

static void obs_manage_irq(struct msm_hs_port *msm_uport, bool en)
{
	struct uart_port *uport = &(msm_uport->uport);

	if (msm_uport->obs) {
		if (en)
			enable_irq(uport->irq);
		else
			disable_irq(uport->irq);
	}
}

static void msm_hs_pm_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct msm_hs_port *msm_uport = get_matching_hs_port(pdev);
	int client_count = 0;

	if (!msm_uport)
		goto err_suspend;
	mutex_lock(&msm_uport->mtx);

	client_count = atomic_read(&msm_uport->client_count);
	msm_uport->pm_state = MSM_HS_PM_SUSPENDED;
	msm_hs_resource_off(msm_uport);
	obs_manage_irq(msm_uport, false);
	msm_hs_clk_bus_unvote(msm_uport);

	if (!atomic_read(&msm_uport->client_req_state))
		enable_wakeup_interrupt(msm_uport);
	LOG_USR_MSG(msm_uport->ipc_msm_hs_pwr_ctxt,
		"%s(): PM State Suspended client_count %d\n", __func__,
								client_count);
	mutex_unlock(&msm_uport->mtx);
	return;
err_suspend:
	pr_err("%s(): invalid uport\n", __func__);
}

static int msm_hs_pm_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct msm_hs_port *msm_uport = get_matching_hs_port(pdev);
	int ret = 0;
	int client_count = 0;

	if (!msm_uport) {
		dev_err(dev, "%s():Invalid uport\n", __func__);
		return -ENODEV;
	}

	mutex_lock(&msm_uport->mtx);
	client_count = atomic_read(&msm_uport->client_count);
	if (msm_uport->pm_state == MSM_HS_PM_ACTIVE)
		goto exit_pm_resume;
	if (!atomic_read(&msm_uport->client_req_state))
		disable_wakeup_interrupt(msm_uport);

	ret = msm_hs_clk_bus_vote(msm_uport);
	if (ret) {
		MSM_HS_ERR("%s():Failed clock vote %d\n", __func__, ret);
		goto exit_pm_resume;
	}
	obs_manage_irq(msm_uport, true);
	msm_uport->pm_state = MSM_HS_PM_ACTIVE;
	msm_hs_resource_on(msm_uport);

	LOG_USR_MSG(msm_uport->ipc_msm_hs_pwr_ctxt,
		"%s():PM State:Active client_count %d\n",
		 __func__, client_count);
exit_pm_resume:
	mutex_unlock(&msm_uport->mtx);
	return ret;
}

#ifdef CONFIG_PM
static int msm_hs_pm_sys_suspend_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct msm_hs_port *msm_uport = get_matching_hs_port(pdev);
	enum msm_hs_pm_state prev_pwr_state;
	int clk_cnt, client_count, ret = 0;

	if (IS_ERR_OR_NULL(msm_uport))
		return -ENODEV;

	mutex_lock(&msm_uport->mtx);

	/*
	 * If there is an active clk request or an impending userspace request
	 * fail the suspend callback.
	 */
	clk_cnt = atomic_read(&msm_uport->resource_count);
	client_count = atomic_read(&msm_uport->client_count);
	if (msm_uport->pm_state == MSM_HS_PM_ACTIVE) {
		MSM_HS_WARN("%s():Fail Suspend.clk_cnt:%d,clnt_count:%d\n",
				 __func__, clk_cnt, client_count);
		ret = -EBUSY;
		goto exit_suspend_noirq;
	}

	prev_pwr_state = msm_uport->pm_state;
	msm_uport->pm_state = MSM_HS_PM_SYS_SUSPENDED;
	LOG_USR_MSG(msm_uport->ipc_msm_hs_pwr_ctxt,
		"%s():PM State:Sys-Suspended client_count %d\n", __func__,
								client_count);
exit_suspend_noirq:
	mutex_unlock(&msm_uport->mtx);
	return ret;
};

static int msm_hs_pm_sys_resume_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct msm_hs_port *msm_uport = get_matching_hs_port(pdev);

	if (IS_ERR_OR_NULL(msm_uport))
		return -ENODEV;
	/*
	 * Note system-pm resume and update the state
	 * variable. Resource activation will be done
	 * when transfer is requested.
	 */

	mutex_lock(&msm_uport->mtx);
	if (msm_uport->pm_state == MSM_HS_PM_SYS_SUSPENDED)
		msm_uport->pm_state = MSM_HS_PM_SUSPENDED;
	LOG_USR_MSG(msm_uport->ipc_msm_hs_pwr_ctxt,
		"%s():PM State: Suspended\n", __func__);
	mutex_unlock(&msm_uport->mtx);
	return 0;
}
#endif

#ifdef CONFIG_PM
static void  msm_serial_hs_rt_init(struct uart_port *uport)
{
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	MSM_HS_DBG("%s(): Enabling runtime pm\n", __func__);
	pm_runtime_set_suspended(uport->dev);
	pm_runtime_set_autosuspend_delay(uport->dev, 250);
	pm_runtime_use_autosuspend(uport->dev);
	mutex_lock(&msm_uport->mtx);
	msm_uport->pm_state = MSM_HS_PM_SUSPENDED;
	mutex_unlock(&msm_uport->mtx);
	pm_runtime_enable(uport->dev);
}

static int msm_hs_runtime_suspend(struct device *dev)
{
	msm_hs_pm_suspend(dev);
	return 0;
}

static int msm_hs_runtime_resume(struct device *dev)
{
	return msm_hs_pm_resume(dev);
}
#else
static void  msm_serial_hs_rt_init(struct uart_port *uport) {}
static int msm_hs_runtime_suspend(struct device *dev) {}
static int msm_hs_runtime_resume(struct device *dev) {}
#endif

static int msm_hs_read_dtsi(struct platform_device *pdev,
			    struct msm_hs_port *msm_uport)
{
	int ret = 0;
	struct resource *core_resource;
	struct resource *bam_resource;
	struct uart_port *uport = &msm_uport->uport;
	int core_irqres, bam_irqres, wakeup_irqres;

	/* Get required resources for BAM HSUART */
	core_resource = platform_get_resource_byname(pdev,
						     IORESOURCE_MEM, "core_mem");
	if (!core_resource) {
		dev_err(&pdev->dev, "Invalid core HSUART Resources\n");
		return -ENXIO;
	}
	bam_resource = platform_get_resource_byname(pdev,
						    IORESOURCE_MEM, "bam_mem");
	if (!bam_resource) {
		dev_err(&pdev->dev, "Invalid BAM HSUART Resources\n");
		return -ENXIO;
	}
	core_irqres = platform_get_irq(pdev, 0);
	if (core_irqres < 0) {
		dev_err(&pdev->dev, "Error %d, invalid core irq resources\n", core_irqres);
		return -ENXIO;
	}
	bam_irqres = platform_get_irq(pdev, 1);
	if (bam_irqres < 0) {
		dev_err(&pdev->dev, "Error %d, invalid bam irq resources\n", bam_irqres);
		return -ENXIO;
	}
	wakeup_irqres = platform_get_irq(pdev, 2);
	if (wakeup_irqres < 0) {
		wakeup_irqres = -1;
		pr_info("Wakeup irq not specified\n");
	}

	irq_set_status_flags(core_irqres, IRQ_NOAUTOEN);
	ret = devm_request_irq(uport->dev, core_irqres, msm_hs_isr,
			       IRQF_TRIGGER_HIGH, "msm_hs_uart", msm_uport);
	if (ret) {
		dev_err(uport->dev, "%s: Failed to get IRQ ret %d\n",
			__func__, ret);
		return ret;
	}

	if (wakeup_irqres > 0) {
		irq_set_status_flags(wakeup_irqres, IRQ_NOAUTOEN);
		ret = devm_request_irq(uport->dev, wakeup_irqres,
				       msm_hs_wakeup_isr,
				       IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				       "msm_hs_wakeup", msm_uport);
		if (unlikely(ret)) {
			dev_err(uport->dev, "%s():Err getting uart wakeup_irq %d\n",
				__func__, ret);
			return ret;
		}
	}

	uport->irq = core_irqres;
	msm_uport->bam_irq = bam_irqres;
	msm_uport->wakeup.irq = wakeup_irqres;

	uport->mapbase = core_resource->start;
	msm_uport->uport.membase = ioremap(uport->mapbase,
					   resource_size(core_resource));
	if (unlikely(!msm_uport->uport.membase)) {
		dev_err(&pdev->dev, "UART Resource ioremap Failed\n");
		return -ENOMEM;
	}

	msm_uport->bam_mem = bam_resource->start;
	msm_uport->bam_base = ioremap(msm_uport->bam_mem,
				      resource_size(bam_resource));
	if (unlikely(!msm_uport->bam_base)) {
		dev_err(&pdev->dev, "UART BAM Resource ioremap Failed\n");
		iounmap(msm_uport->uport.membase);
		return -ENOMEM;
	}
	return ret;
}

static int msm_hs_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct uart_port *uport;
	struct msm_hs_port *msm_uport;
	struct msm_serial_hs_platform_data *pdata = pdev->dev.platform_data;
	unsigned long data;
	char name[30];

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(&pdev->dev, "could not set DMA mask\n");
		return ret;
	}

	if (pdev->dev.of_node) {
		dev_dbg(&pdev->dev, "device tree enabled\n");
		pdata = msm_hs_dt_to_pdata(pdev);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);

		if (pdev->id < 0) {
			pdev->id = device_id_grab_next_free();
			if (pdev->id < 0) {
				dev_err(&pdev->dev,
					"Error grabbing next free device id\n");
				return pdev->id;
			}
		} else {
			ret = device_id_set_used(pdev->id);
			if (ret < 0) {
				dev_warn(&pdev->dev, "%d alias taken\n",
					pdev->id);
				return ret;
			}
		}
		pdev->dev.platform_data = pdata;
	}

	if (pdev->id < 0 || pdev->id >= UARTDM_NR) {
		dev_err(&pdev->dev, "Invalid plaform device ID = %d\n",
								pdev->id);
		return -EINVAL;
	}

	msm_uport = devm_kzalloc(&pdev->dev, sizeof(struct msm_hs_port),
			GFP_KERNEL);
	if (!msm_uport)
		return -ENOMEM;

	msm_uport->uport.type = PORT_UNKNOWN;
	uport = &msm_uport->uport;
	uport->dev = &pdev->dev;

	if (pdev->dev.of_node)
		msm_uport->uart_type = BLSP_HSUART;

	msm_hs_get_pinctrl_configs(uport);
	ret = msm_hs_read_dtsi(pdev, msm_uport);
	if (ret) {
		MSM_HS_ERR("%s():Error while reading dtsi:%d\n", __func__, ret);
		return ret;
	}

	memset(name, 0, sizeof(name));
	scnprintf(name, sizeof(name), "%s%s", dev_name(msm_uport->uport.dev),
									"_state");
	msm_uport->ipc_msm_hs_log_ctxt =
			ipc_log_context_create(IPC_MSM_HS_LOG_STATE_PAGES,
								name, 0);
	if (!msm_uport->ipc_msm_hs_log_ctxt) {
		dev_err(&pdev->dev, "%s(): error creating logging context\n",
								__func__);
	} else {
		msm_uport->ipc_debug_mask = INFO_LEV;
		ret = sysfs_create_file(&pdev->dev.kobj,
				&dev_attr_debug_mask.attr);
		if (unlikely(ret))
			MSM_HS_WARN("%s(): Failed create dev. attr\n",
			 __func__);
	}

	msm_uport->icc_path = of_icc_get(&pdev->dev, "blsp-ddr");
	if (IS_ERR_OR_NULL(msm_uport->icc_path)) {
		ret = msm_uport->icc_path ?
			PTR_ERR(msm_uport->icc_path) : -EINVAL;
		dev_err(&pdev->dev, "%s(): failed to get ICC path: %d\n", __func__, ret);
		msm_uport->icc_path = NULL;
		goto unmap_memory;
	}
	pdata->wakeup_irq = msm_uport->wakeup.irq;
	msm_uport->wakeup.ignore = 1;
	msm_uport->wakeup.inject_rx = pdata->inject_rx_on_wakeup;
	msm_uport->wakeup.rx_to_inject = pdata->rx_to_inject;
	msm_uport->obs = pdata->obs;

	msm_uport->bam_tx_ep_pipe_index =
			pdata->bam_tx_ep_pipe_index;
	msm_uport->bam_rx_ep_pipe_index =
			pdata->bam_rx_ep_pipe_index;
	msm_uport->wakeup.enabled = false;

	uport->iotype = UPIO_MEM;
	uport->fifosize = 64;
	uport->ops = &msm_hs_ops;
	uport->flags = UPF_BOOT_AUTOCONF;
	uport->uartclk = 7372800;
	msm_uport->imr_reg = 0x0;

	msm_uport->clk = clk_get(&pdev->dev, "core_clk");
	if (IS_ERR(msm_uport->clk)) {
		ret = PTR_ERR(msm_uport->clk);
		goto deregister_bus_client;
	}

	msm_uport->pclk = clk_get(&pdev->dev, "iface_clk");
	/*
	 * Some configurations do not require explicit pclk control so
	 * do not flag error on pclk get failure.
	 */
	if (IS_ERR(msm_uport->pclk))
		msm_uport->pclk = NULL;

	msm_uport->hsuart_wq = alloc_workqueue("k_hsuart",
					WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (!msm_uport->hsuart_wq) {
		MSM_HS_ERR("%s(): Unable to create workqueue hsuart_wq\n",
								__func__);
		ret =  -ENOMEM;
		goto put_clk;
	}

	mutex_init(&msm_uport->mtx);

	/* Initialize SPS HW connected with UART core */
	ret = msm_hs_sps_init(msm_uport);
	if (unlikely(ret)) {
		MSM_HS_ERR("SPS Initialization failed, err=%d\n", ret);
		goto destroy_mutex;
	}

	msm_uport->tx.flush = FLUSH_SHUTDOWN;
	msm_uport->rx.flush = FLUSH_SHUTDOWN;

	memset(name, 0, sizeof(name));
	scnprintf(name, sizeof(name), "%s%s", dev_name(msm_uport->uport.dev),
									"_tx");
	msm_uport->tx.ipc_tx_ctxt =
		ipc_log_context_create(IPC_MSM_HS_LOG_DATA_PAGES, name, 0);
	if (!msm_uport->tx.ipc_tx_ctxt)
		dev_err(&pdev->dev, "%s(): error creating tx log context\n",
								__func__);

	memset(name, 0, sizeof(name));
	scnprintf(name, sizeof(name), "%s%s", dev_name(msm_uport->uport.dev),
									"_rx");
	msm_uport->rx.ipc_rx_ctxt = ipc_log_context_create(
					IPC_MSM_HS_LOG_DATA_PAGES, name, 0);
	if (!msm_uport->rx.ipc_rx_ctxt)
		dev_err(&pdev->dev, "%s(): error creating rx log context\n",
								__func__);

	memset(name, 0, sizeof(name));
	scnprintf(name, sizeof(name), "%s%s", dev_name(msm_uport->uport.dev),
									"_pwr");
	msm_uport->ipc_msm_hs_pwr_ctxt = ipc_log_context_create(
					IPC_MSM_HS_LOG_USER_PAGES, name, 0);
	if (!msm_uport->ipc_msm_hs_pwr_ctxt)
		dev_err(&pdev->dev, "%s(): error creating usr log context\n",
								__func__);

	clk_set_rate(msm_uport->clk, msm_uport->uport.uartclk);
	msm_hs_clk_bus_vote(msm_uport);
	ret = uartdm_init_port(uport);
	if (unlikely(ret))
		goto err_clock;

	/* configure the CR Protection to Enable */
	msm_hs_write(uport, UART_DM_CR, CR_PROTECTION_EN);

	/*
	 * Enable Command register protection before going ahead as this hw
	 * configuration makes sure that issued cmd to CR register gets complete
	 * before next issued cmd start. Hence mb() requires here.
	 */
	mb();

	/*
	 * Set RX_BREAK_ZERO_CHAR_OFF and RX_ERROR_CHAR_OFF
	 * so any rx_break and character having parity of framing
	 * error don't enter inside UART RX FIFO.
	 */
	data = msm_hs_read(uport, UART_DM_MR2);
	data |= (UARTDM_MR2_RX_BREAK_ZERO_CHAR_OFF |
			UARTDM_MR2_RX_ERROR_CHAR_OFF);
	msm_hs_write(uport, UART_DM_MR2, data);
	/* Ensure register IO completion */
	mb();

	ret = sysfs_create_file(&pdev->dev.kobj, &dev_attr_clock.attr);
	if (unlikely(ret)) {
		MSM_HS_ERR("Probe Failed as sysfs failed\n");
		goto err_clock;
	}

	msm_serial_debugfs_init(msm_uport, pdev->id);
	msm_hs_unconfig_uart_gpios(uport);

	uport->line = pdev->id;
	if (pdata->userid && pdata->userid <= UARTDM_NR)
		uport->line = pdata->userid;
	ret = uart_add_one_port(&msm_hs_driver, uport);
	if (!ret) {
		msm_hs_clk_bus_unvote(msm_uport);
		msm_serial_hs_rt_init(uport);
		return ret;
	}

err_clock:
	msm_hs_clk_bus_unvote(msm_uport);

destroy_mutex:
	mutex_destroy(&msm_uport->mtx);
	destroy_workqueue(msm_uport->hsuart_wq);

put_clk:
	if (msm_uport->pclk)
		clk_put(msm_uport->pclk);

	if (msm_uport->clk)
		clk_put(msm_uport->clk);

deregister_bus_client:
	if (msm_uport->icc_path) {
		icc_put(msm_uport->icc_path);
		msm_uport->icc_path = NULL;
	}
unmap_memory:
	iounmap(msm_uport->uport.membase);
	iounmap(msm_uport->bam_base);

	return ret;
}

static int __init msm_serial_hs_init(void)
{
	int ret;

	ret = uart_register_driver(&msm_hs_driver);
	if (unlikely(ret)) {
		pr_err("%s failed to load\n", __func__);
		return ret;
	}
	debug_base = debugfs_create_dir("msm_serial_hs", NULL);
	if (IS_ERR_OR_NULL(debug_base))
		pr_err("msm_serial_hs: Cannot create debugfs dir\n");

	ret = platform_driver_register(&msm_serial_hs_platform_driver);
	if (ret) {
		pr_err("%s failed to load\n", __func__);
		debugfs_remove_recursive(debug_base);
		uart_unregister_driver(&msm_hs_driver);
		return ret;
	}

	pr_debug("msm_serial_hs module loaded\n");
	return ret;
}

/*
 *  Called by the upper layer when port is closed.
 *     - Disables the port
 *     - Unhook the ISR
 */
static void msm_hs_shutdown(struct uart_port *uport)
{
	int ret, rc;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);
	struct circ_buf *tx_buf = &uport->state->xmit;
	int data;
	unsigned long flags;

	if (msm_uport->wakeup.enabled) {
		ret = irq_set_irq_wake(msm_uport->wakeup.irq, 0);
		if (unlikely(ret))
			MSM_HS_WARN("%s:Failed to unset IRQ wake:%d\n", __func__, ret);
		disable_irq(msm_uport->wakeup.irq);
		spin_lock_irqsave(&uport->lock, flags);
		msm_uport->wakeup.enabled = false;
		msm_uport->wakeup.ignore = 1;
		spin_unlock_irqrestore(&uport->lock, flags);
	} else {
		disable_irq(uport->irq);
	}

	/* make sure tx lh finishes */
	kthread_flush_worker(&msm_uport->tx.kworker);
	ret = wait_event_timeout(msm_uport->tx.wait,
			uart_circ_empty(tx_buf), 500);
	if (!ret)
		MSM_HS_WARN("Shutdown called when tx buff not empty\n");

	ret = pinctrl_select_state(msm_uport->pinctrl,
				   msm_uport->gpio_state_shutdown);
	if (ret)
		MSM_HS_WARN("%s():Error selecting shutdown state:%d\n", __func__, ret);

	msm_hs_resource_vote(msm_uport);
	/* Stop remote side from sending data */
	msm_hs_disable_flow_control(uport, false);
	/* make sure rx lh finishes */
	kthread_flush_worker(&msm_uport->rx.kworker);

	if (msm_uport->rx.flush != FLUSH_SHUTDOWN) {
		/* disable and disconnect rx */
		ret = wait_event_timeout(msm_uport->rx.wait,
				!msm_uport->rx.pending_flag, 500);
		if (!ret)
			MSM_HS_WARN("%s(): rx disconnect not complete\n",
				__func__);
		msm_hs_disconnect_rx(uport);
	}

	cancel_delayed_work_sync(&msm_uport->rx.flip_insert_work);
	flush_workqueue(msm_uport->hsuart_wq);

	/* BAM Disconnect for TX */
	data = msm_hs_read(uport, UART_DM_DMEN);
	data &= ~UARTDM_TX_BAM_ENABLE_BMSK;
	msm_hs_write(uport, UART_DM_DMEN, data);
	ret = sps_tx_disconnect(msm_uport);
	if (ret)
		MSM_HS_ERR("%s(): sps_disconnect failed\n",
					__func__);
	msm_uport->tx.flush = FLUSH_SHUTDOWN;
	/* Disable the transmitter */
	msm_hs_write(uport, UART_DM_CR, UARTDM_CR_TX_DISABLE_BMSK);
	/* Disable the receiver */
	msm_hs_write(uport, UART_DM_CR, UARTDM_CR_RX_DISABLE_BMSK);

	msm_uport->imr_reg = 0;
	msm_hs_write(uport, UART_DM_IMR, msm_uport->imr_reg);
	/*
	 * Complete all device write before actually disabling uartclk.
	 * Hence mb() requires here.
	 */
	mb();

	msm_uport->rx.buffer_pending = NONE_PENDING;
	MSM_HS_DBG("%s(): tx, rx events complete\n", __func__);

	dma_unmap_single(uport->dev, msm_uport->tx.dma_base,
			 UART_XMIT_SIZE, DMA_TO_DEVICE);

	msm_hs_resource_unvote(msm_uport);
	rc = atomic_read(&msm_uport->resource_count);
	if (rc) {
		atomic_set(&msm_uport->resource_count, 1);
		MSM_HS_WARN("%s(): removing extra vote\n", __func__);
	}
	if (atomic_read(&msm_uport->client_req_state)) {
		MSM_HS_WARN("%s(): Client clock vote imbalance\n", __func__);
		atomic_set(&msm_uport->client_req_state, 0);
	}
	if (atomic_read(&msm_uport->client_count)) {
		MSM_HS_WARN("%s(): Client vote on, forcing to 0\n", __func__);
		atomic_set(&msm_uport->client_count, 0);
		LOG_USR_MSG(msm_uport->ipc_msm_hs_pwr_ctxt,
			"%s(): Client_Count 0\n", __func__);
	}
	msm_hs_unconfig_uart_gpios(uport);
	MSM_HS_INFO("%s():UART port closed successfully\n", __func__);
}

static void __exit msm_serial_hs_exit(void)
{
	pr_debug("msm_serial_hs module removed\n");
	debugfs_remove_recursive(debug_base);
	platform_driver_unregister(&msm_serial_hs_platform_driver);
	uart_unregister_driver(&msm_hs_driver);
}

static const struct dev_pm_ops msm_hs_dev_pm_ops = {
	.runtime_suspend = msm_hs_runtime_suspend,
	.runtime_resume = msm_hs_runtime_resume,
	.runtime_idle = NULL,
	.suspend_noirq = msm_hs_pm_sys_suspend_noirq,
	.resume_noirq = msm_hs_pm_sys_resume_noirq,
};

static struct platform_driver msm_serial_hs_platform_driver = {
	.probe	= msm_hs_probe,
	.remove = msm_hs_remove,
	.driver = {
		.name = "msm_serial_hs",
		.pm   = &msm_hs_dev_pm_ops,
		.of_match_table = msm_hs_match_table,
	},
};

static struct uart_driver msm_hs_driver = {
	.owner = THIS_MODULE,
	.driver_name = "msm_serial_hs",
	.dev_name = "ttyHS",
	.nr = UARTDM_NR,
	.cons = 0,
};

static const struct uart_ops msm_hs_ops = {
	.tx_empty = msm_hs_tx_empty,
	.set_mctrl = msm_hs_set_mctrl_locked,
	.get_mctrl = msm_hs_get_mctrl_locked,
	.stop_tx = msm_hs_stop_tx_locked,
	.start_tx = msm_hs_start_tx_locked,
	.stop_rx = msm_hs_stop_rx_locked,
	.enable_ms = msm_hs_enable_ms_locked,
	.break_ctl = msm_hs_break_ctl,
	.startup = msm_hs_startup,
	.shutdown = msm_hs_shutdown,
	.set_termios = msm_hs_set_termios,
	.type = msm_hs_type,
	.config_port = msm_hs_config_port,
	.flush_buffer = NULL,
	.ioctl = msm_hs_ioctl,
};

module_init(msm_serial_hs_init);
module_exit(msm_serial_hs_exit);
MODULE_DESCRIPTION("High Speed UART Driver for the MSM chipset");
MODULE_LICENSE("GPL");

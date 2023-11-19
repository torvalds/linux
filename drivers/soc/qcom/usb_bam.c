// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/panic_notifier.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/msm-sps.h>
#include <linux/sched/clock.h>
#include <linux/usb_bam.h>
#include <linux/workqueue.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/usb/msm_hsusb.h>

#define USB_THRESHOLD 512
#define USB_BAM_MAX_STR_LEN 50
#define DBG_MAX_MSG   512UL
#define DBG_MSG_LEN   160UL
#define TIME_BUF_LEN  17
#define DBG_EVENT_LEN  143

#define LOGLEVEL_NONE 8
#define LOGLEVEL_DEBUG 7
#define LOGLEVEL_ERR 3

#define log_event(log_level, x...)					\
do {									\
	unsigned long flags;						\
	char *buf;							\
	if (log_level == LOGLEVEL_DEBUG)				\
		pr_debug(x);						\
	else if (log_level == LOGLEVEL_ERR)				\
		pr_err(x);						\
	write_lock_irqsave(&usb_bam_dbg.lck, flags);			\
	buf = usb_bam_dbg.buf[usb_bam_dbg.idx];				\
	put_timestamp(buf);						\
	snprintf(&buf[TIME_BUF_LEN - 1], DBG_EVENT_LEN, x);		\
	usb_bam_dbg.idx = (usb_bam_dbg.idx + 1) % DBG_MAX_MSG;		\
	write_unlock_irqrestore(&usb_bam_dbg.lck, flags);		\
} while (0)

#define log_event_none(x, ...) log_event(LOGLEVEL_NONE, x, ##__VA_ARGS__)
#define log_event_dbg(x, ...) log_event(LOGLEVEL_DEBUG, x, ##__VA_ARGS__)
#define log_event_err(x, ...) log_event(LOGLEVEL_ERR, x, ##__VA_ARGS__)

/* Reset BAM with pipes connected */
#define SPS_BAM_FORCE_RESET         (1UL << 11)

enum usb_bam_event_type {
	USB_BAM_EVENT_WAKEUP_PIPE = 0,	/* Wake a pipe */
	USB_BAM_EVENT_WAKEUP,		/* Wake a bam (first pipe waked) */
	USB_BAM_EVENT_INACTIVITY,	/* Inactivity on all pipes */
};

struct usb_bam_sps_type {
	struct sps_pipe **sps_pipes;
	struct sps_connect *sps_connections;
};

/*
 * struct usb_bam_event_info: suspend/resume event information.
 * @type: usb bam event type.
 * @event: holds event data.
 * @callback: suspend/resume callback.
 * @param: port num (for suspend) or NULL (for resume).
 * @event_w: holds work queue parameters.
 */
struct usb_bam_event_info {
	enum usb_bam_event_type type;
	struct sps_register_event event;
	int (*callback)(void *ptr);
	void *param;
	struct work_struct event_w;
};

/*
 * struct usb_bam_pipe_connect: pipe connection information
 * between USB/HSIC BAM and another BAM. USB/HSIC BAM can be
 * either src BAM or dst BAM
 * @name: pipe description.
 * @mem_type: type of memory used for BAM FIFOs
 * @src_phy_addr: src bam physical address.
 * @src_pipe_index: src bam pipe index.
 * @dst_phy_addr: dst bam physical address.
 * @dst_pipe_index: dst bam pipe index.
 * @data_fifo_base_offset: data fifo offset.
 * @data_fifo_size: data fifo size.
 * @desc_fifo_base_offset: descriptor fifo offset.
 * @desc_fifo_size: descriptor fifo size.
 * @data_mem_buf: data fifo buffer.
 * @desc_mem_buf: descriptor fifo buffer.
 * @event: event for wakeup.
 * @enabled: true if pipe is enabled.
 * @suspended: true if pipe is suspended.
 * @cons_stopped: true is pipe has consumer requests stopped.
 * @prod_stopped: true if pipe has producer requests stopped.
 * @priv: private data to return upon activity_notify
 *	or inactivity_notify callbacks.
 * @activity_notify: callback to invoke on activity on one of the in pipes.
 * @inactivity_notify: callback to invoke on inactivity on all pipes.
 * @start: callback to invoke to enqueue transfers on a pipe.
 * @stop: callback to invoke on dequeue transfers on a pipe.
 * @start_stop_param: param for the start/stop callbacks.
 */
struct usb_bam_pipe_connect {
	const char *name;
	u32 pipe_num;
	enum usb_pipe_mem_type mem_type;
	enum usb_bam_pipe_dir dir;
	enum usb_ctrl bam_type;
	enum peer_bam peer_bam;
	enum usb_bam_pipe_type pipe_type;
	u32 src_phy_addr;
	u32 src_pipe_index;
	u32 dst_phy_addr;
	u32 dst_pipe_index;
	u32 data_fifo_base_offset;
	u32 data_fifo_size;
	u32 desc_fifo_base_offset;
	u32 desc_fifo_size;
	struct sps_mem_buffer data_mem_buf;
	struct sps_mem_buffer desc_mem_buf;
	struct usb_bam_event_info event;
	bool enabled;
	bool suspended;
	bool cons_stopped;
	bool prod_stopped;
	void *priv;
	int (*activity_notify)(void *priv);
	int (*inactivity_notify)(void *priv);
	void (*start)(void *ptr, enum usb_bam_pipe_dir);
	void (*stop)(void *ptr, enum usb_bam_pipe_dir);
	void *start_stop_param;
	bool reset_pipe_after_lpm;
};

/**
 * struct msm_usb_bam_data: pipe connection information
 * between USB/HSIC BAM and another BAM. USB/HSIC BAM can be
 * either src BAM or dst BAM
 * @usb_bam_num_pipes: max number of pipes to use.
 * @active_conn_num: number of active pipe connections.
 * @usb_bam_fifo_baseaddr: base address for bam pipe's data and descriptor
 *                         fifos. This can be on chip memory (ocimem) or usb
 *                         private memory.
 * @reset_on_connect: BAM must be reset before its first pipe connect
 * @reset_on_disconnect: BAM must be reset after its last pipe disconnect
 * @disable_clk_gating: Disable clock gating
 * @override_threshold: Override the default threshold value for Read/Write
 *                         event generation by the BAM towards another BAM.
 * @max_mbps_highspeed: Maximum Mbits per seconds that the USB core
 *		can work at in bam2bam mode when connected to HS host.
 * @max_mbps_superspeed: Maximum Mbits per seconds that the USB core
 *		can work at in bam2bam mode when connected to SS host.
 */
struct msm_usb_bam_data {
	u8 max_connections;
	int usb_bam_num_pipes;
	phys_addr_t usb_bam_fifo_baseaddr;
	bool reset_on_connect;
	bool reset_on_disconnect;
	bool disable_clk_gating;
	bool ignore_core_reset_ack;
	u32 override_threshold;
	u32 max_mbps_highspeed;
	u32 max_mbps_superspeed;
	enum usb_ctrl bam_type;
};

/*
 * struct usb_bam_ctx_type - represents the usb bam driver entity
 * @usb_bam_sps: holds the sps pipes the usb bam driver holds
 *	against the sps driver.
 * @usb_bam_pdev: the platform device that represents the usb bam.
 * @usb_bam_wq: Worqueue used for managing states of reset against
 *	a peer bam.
 * @max_connections: The maximum number of pipes that are configured
 *	in the platform data.
 * @h_bam: the handle/device of the sps driver.
 * @pipes_enabled_per_bam: the number of pipes currently enabled.
 * @inactivity_timer_ms: The timeout configuration per each bam for inactivity
 *	timer feature.
 * @is_bam_inactivity: Is there no activity on all pipes belongs to a
 *	specific bam. (no activity = no data is pulled or pushed
 *	from/into ones of the pipes).
 * @usb_bam_connections: array (allocated on probe) having all BAM connections
 * @usb_bam_lock: to protect fields of ctx or usb_bam_connections
 */
struct usb_bam_ctx_type {
	struct usb_bam_sps_type		usb_bam_sps;
	struct resource			*io_res;
	int				irq;
	struct platform_device		*usb_bam_pdev;
	struct workqueue_struct		*usb_bam_wq;
	u8				max_connections;
	unsigned long			h_bam;
	u8				pipes_enabled_per_bam;
	u32				inactivity_timer_ms;
	bool				is_bam_inactivity;
	struct usb_bam_pipe_connect	*usb_bam_connections;
	struct msm_usb_bam_data *usb_bam_data;
	spinlock_t		usb_bam_lock;
};

static char *bam_enable_strings[MAX_BAMS] = {
	[DWC3_CTRL] = "ssusb",
	[CI_CTRL] = "hsusb",
	[HSIC_CTRL]  = "hsic",
};

struct usb_bam_host_info {
	struct device *dev;
	bool in_lpm;
};

static struct usb_bam_host_info host_info[MAX_BAMS];
static struct usb_bam_ctx_type msm_usb_bam[MAX_BAMS];
/* USB bam type used as a peer of the qdss in bam2bam mode */
static enum usb_ctrl qdss_usb_bam_type;

static int __usb_bam_register_wake_cb(enum usb_ctrl bam_type, int idx,
				      int (*callback)(void *user),
	void *param, bool trigger_cb_per_pipe);

static struct {
	char buf[DBG_MAX_MSG][DBG_MSG_LEN];   /* buffer */
	unsigned int idx;   /* index */
	rwlock_t lck;   /* lock */
} __maybe_unused usb_bam_dbg = {
	.idx = 0,
	.lck = __RW_LOCK_UNLOCKED(lck)
};

/*put_timestamp - writes time stamp to buffer */
static void __maybe_unused put_timestamp(char *tbuf)
{
	unsigned long long t;
	unsigned long nanosec_rem;

	t = cpu_clock(smp_processor_id());
	nanosec_rem = do_div(t, 1000000000)/1000;
	snprintf(tbuf, TIME_BUF_LEN, "[%5lu.%06lu]: ", (unsigned long)t,
		nanosec_rem);
}

void msm_bam_set_hsic_host_dev(struct device *dev)
{
	if (dev) {
		/* Hold the device until allowing lpm */
		log_event_dbg("%s: Getting hsic device %pK\n", __func__, dev);
		pm_runtime_get(dev);
	} else if (host_info[HSIC_CTRL].dev) {
		log_event_dbg("%s: Try Putting hsic device %pK, lpm:%d\n",
				__func__, host_info[HSIC_CTRL].dev,
				host_info[HSIC_CTRL].in_lpm);
		/* Just release previous device if not already done */
		if (!host_info[HSIC_CTRL].in_lpm) {
			host_info[HSIC_CTRL].in_lpm = true;
			pm_runtime_put(host_info[HSIC_CTRL].dev);
		}
	}

	host_info[HSIC_CTRL].dev = dev;
	host_info[HSIC_CTRL].in_lpm = false;
}

static void usb_bam_set_inactivity_timer(enum usb_ctrl bam)
{
	struct sps_timer_ctrl timer_ctrl;
	struct usb_bam_pipe_connect *pipe_connect;
	struct sps_pipe *pipe = NULL;
	struct usb_bam_ctx_type *ctx = &msm_usb_bam[bam];
	int i;

	/*
	 * Since we configure global incativity timer for all pipes
	 * and not per each pipe, it is enough to use some pipe
	 * handle associated with this bam, so just find the first one.
	 * This pipe handle is required due to SPS driver API we use below.
	 */
	for (i = 0; i < ctx->max_connections; i++) {
		pipe_connect = &ctx->usb_bam_connections[i];
		if (pipe_connect->bam_type == bam && pipe_connect->enabled) {
			pipe = ctx->usb_bam_sps.sps_pipes[i];
			break;
		}
	}

	if (!pipe) {
		pr_warn("%s: Bam has no connected pipes\n", __func__);
		return;
	}

	timer_ctrl.op = SPS_TIMER_OP_CONFIG;
	timer_ctrl.mode = SPS_TIMER_MODE_ONESHOT;
	timer_ctrl.timeout_msec = ctx->inactivity_timer_ms;
	sps_timer_ctrl(pipe, &timer_ctrl, NULL);

	timer_ctrl.op = SPS_TIMER_OP_RESET;
	sps_timer_ctrl(pipe, &timer_ctrl, NULL);
}

static void _msm_bam_host_notify_on_resume(enum usb_ctrl bam_type)
{
	struct usb_bam_ctx_type *ctx = &msm_usb_bam[bam_type];

	spin_lock(&ctx->usb_bam_lock);
	log_event_dbg("%s: enter bam=%s\n", __func__,
			bam_enable_strings[bam_type]);

	host_info[bam_type].in_lpm = false;

	/*
	 * This function is called to notify the usb bam driver
	 * that the hsic core and hsic bam hw are fully resumed
	 * and clocked on. Therefore we can now set the inactivity
	 * timer to the hsic bam hw.
	 */
	if (ctx->inactivity_timer_ms)
		usb_bam_set_inactivity_timer(bam_type);

	spin_unlock(&ctx->usb_bam_lock);
}

/**
 * msm_bam_hsic_host_pipe_empty - Check all HSIC host BAM pipe state
 *
 * return true if all BAM pipe used for HSIC Host mode is empty.
 */
bool msm_bam_hsic_host_pipe_empty(void)
{
	struct usb_bam_pipe_connect *pipe_connect;
	struct sps_pipe *pipe = NULL;
	enum usb_ctrl bam = HSIC_CTRL;
	struct usb_bam_ctx_type *ctx = &msm_usb_bam[bam];
	int i, ret;
	u32 status;

	for (i = 0; i < ctx->max_connections; i++) {
		pipe_connect = &ctx->usb_bam_connections[i];
		if (pipe_connect->enabled) {
			pipe = ctx->usb_bam_sps.sps_pipes[i];
			ret = sps_is_pipe_empty(pipe, &status);
			if (ret) {
				log_event_err("%s(): sps_is_pipe_empty() failed\n",
								__func__);
				log_event_err("%s(): SRC index(%d), DEST index(%d):\n",
						__func__,
						pipe_connect->src_pipe_index,
						pipe_connect->dst_pipe_index);
				WARN_ON(1);
			}

			if (!status) {
				log_event_err("%s(): pipe is not empty.\n",
						__func__);
				log_event_err("%s(): SRC index(%d), DEST index(%d):\n",
						__func__,
						pipe_connect->src_pipe_index,
						pipe_connect->dst_pipe_index);
				return false;
			}

			pr_debug("%s(): SRC index(%d), DEST index(%d):\n",
				  __func__,
				  pipe_connect->src_pipe_index,
				  pipe_connect->dst_pipe_index);
		}

	}

	if (!pipe)
		log_event_err("%s: Bam %s has no connected pipes\n", __func__,
						bam_enable_strings[bam]);

	return true;
}
EXPORT_SYMBOL_GPL(msm_bam_hsic_host_pipe_empty);

static bool msm_bam_host_lpm_ok(enum usb_ctrl bam_type)
{
	struct usb_bam_ctx_type *ctx = &msm_usb_bam[bam_type];
	struct usb_bam_pipe_connect *pipe_iter;
	int i;

	log_event_dbg("%s: enter bam=%s\n", __func__,
			bam_enable_strings[bam_type]);

	if (!host_info[bam_type].dev)
		return true;

	log_event_dbg("%s: Starting hsic full suspend sequence\n",
		__func__);

	pr_debug("%s(): checking HSIC Host pipe state\n", __func__);
	if (!msm_bam_hsic_host_pipe_empty()) {
		log_event_err("%s(): HSIC HOST Pipe is not empty\n",
				__func__);
		return false;
	}

	/* HSIC host will go now to lpm */
	log_event_dbg("%s: vote for suspend hsic %pK\n",
		__func__, host_info[bam_type].dev);

	for (i = 0; i < ctx->max_connections; i++) {
		pipe_iter = &ctx->usb_bam_connections[i];
		if (pipe_iter->bam_type == bam_type &&
		    pipe_iter->enabled && !pipe_iter->suspended)
			pipe_iter->suspended = true;
	}

	host_info[bam_type].in_lpm = true;

	return true;
}

bool msm_bam_hsic_lpm_ok(void)
{
	return msm_bam_host_lpm_ok(HSIC_CTRL);
}
EXPORT_SYMBOL_GPL(msm_bam_hsic_lpm_ok);

void msm_bam_hsic_host_notify_on_resume(void)
{
	_msm_bam_host_notify_on_resume(HSIC_CTRL);
}

static inline enum usb_ctrl get_bam_type_from_core_name(const char *name)
{
	if (strnstr(name, bam_enable_strings[DWC3_CTRL],
			USB_BAM_MAX_STR_LEN) ||
		strnstr(name, "dwc3", USB_BAM_MAX_STR_LEN))
		return DWC3_CTRL;
	else if (strnstr(name, bam_enable_strings[HSIC_CTRL],
			USB_BAM_MAX_STR_LEN) ||
		strnstr(name, "ci13xxx_msm_hsic", USB_BAM_MAX_STR_LEN))
		return HSIC_CTRL;
	else if (strnstr(name, bam_enable_strings[CI_CTRL],
			USB_BAM_MAX_STR_LEN) ||
		strnstr(name, "ci", USB_BAM_MAX_STR_LEN))
		return CI_CTRL;

	log_event_err("%s: invalid BAM name(%s)\n", __func__, name);
	return -EINVAL;
}

static int usb_bam_alloc_buffer(struct usb_bam_pipe_connect *pipe_connect)
{
	int ret = 0;
	struct usb_bam_ctx_type *ctx = &msm_usb_bam[pipe_connect->bam_type];
	struct sps_mem_buffer *data_buf = &(pipe_connect->data_mem_buf);
	struct sps_mem_buffer *desc_buf = &(pipe_connect->desc_mem_buf);
	struct device *dev = &ctx->usb_bam_pdev->dev;
	struct sg_table data_sgt, desc_sgt;
	dma_addr_t data_iova, desc_iova;

	pr_debug("%s: data_fifo size:%x desc_fifo_size:%x\n",
				__func__, pipe_connect->data_fifo_size,
				pipe_connect->desc_fifo_size);

	if (dev->parent)
		dev = dev->parent;

	switch (pipe_connect->mem_type) {
	case SPS_PIPE_MEM:
		log_event_dbg("%s: USB BAM using SPS pipe memory\n", __func__);
		ret = sps_setup_bam2bam_fifo(data_buf,
				pipe_connect->data_fifo_base_offset,
				pipe_connect->data_fifo_size, 1);
		if (ret) {
			log_event_err("%s: data fifo setup failure %d\n",
					__func__, ret);
			goto err_exit;
		}

		ret = sps_setup_bam2bam_fifo(desc_buf,
				pipe_connect->desc_fifo_base_offset,
				pipe_connect->desc_fifo_size, 1);
		if (ret) {
			log_event_err("%s: desc. fifo setup failure %d\n",
					__func__, ret);
			goto err_exit;
		}
		break;
	case OCI_MEM:
		if (pipe_connect->mem_type == OCI_MEM)
			log_event_dbg("%s: USB BAM using ocimem\n", __func__);

		if (data_buf->base) {
			log_event_err("%s: Already allocated OCI Memory\n",
								__func__);
			break;
		}

		data_buf->phys_base = pipe_connect->data_fifo_base_offset +
				ctx->usb_bam_data->usb_bam_fifo_baseaddr;
		data_buf->size = pipe_connect->data_fifo_size;
		data_buf->base = ioremap(data_buf->phys_base, data_buf->size);
		if (!data_buf->base) {
			log_event_err("%s: ioremap failed for data fifo\n",
					__func__);
			ret = -ENOMEM;
			goto err_exit;
		}

		memset_io(data_buf->base, 0, data_buf->size);
		data_buf->iova = dma_map_resource(dev, data_buf->phys_base,
					data_buf->size, DMA_BIDIRECTIONAL, 0);
		if (dma_mapping_error(dev, data_buf->iova))
			log_event_err("%s(): oci_mem: err mapping data_buf\n",
								__func__);
		log_event_dbg("%s: data_buf:%s virt:%pK, phys:%lx, iova:%lx\n",
			__func__, dev_name(dev), data_buf->base,
			(unsigned long)data_buf->phys_base, data_buf->iova);

		desc_buf->phys_base = pipe_connect->desc_fifo_base_offset +
				ctx->usb_bam_data->usb_bam_fifo_baseaddr;
		desc_buf->size = pipe_connect->desc_fifo_size;
		desc_buf->base = ioremap(desc_buf->phys_base, desc_buf->size);
		if (!desc_buf->base) {
			log_event_err("%s: ioremap failed for desc fifo\n",
					__func__);
			iounmap(data_buf->base);
			ret = -ENOMEM;
			goto err_exit;
		}
		memset_io(desc_buf->base, 0, desc_buf->size);
		desc_buf->iova = dma_map_resource(dev, desc_buf->phys_base,
					desc_buf->size,
					DMA_BIDIRECTIONAL, 0);
		if (dma_mapping_error(dev, desc_buf->iova))
			log_event_err("%s(): oci_mem: err mapping desc_buf\n",
								__func__);

		log_event_dbg("%s: desc_buf:%s virt:%pK, phys:%lx, iova:%lx\n",
			__func__, dev_name(dev), desc_buf->base,
			(unsigned long)desc_buf->phys_base, desc_buf->iova);
		break;
	case SYSTEM_MEM:
		log_event_dbg("%s: USB BAM using system memory\n", __func__);

		if (data_buf->base) {
			log_event_err("%s: Already allocated memory\n",
								__func__);
			break;
		}

		/* BAM would use system memory, allocate FIFOs */
		data_buf->base = dma_alloc_attrs(dev,
						pipe_connect->data_fifo_size,
						&data_iova, GFP_KERNEL,
						DMA_ATTR_FORCE_CONTIGUOUS);
		if (!data_buf->base) {
			log_event_err("%s: data_fifo: dma_alloc_attr failed\n",
								__func__);
			ret = -ENOMEM;
			goto err_exit;
		}
		memset(data_buf->base, 0, pipe_connect->data_fifo_size);

		data_buf->iova = data_iova;
		dma_get_sgtable(dev, &data_sgt, data_buf->base, data_buf->iova,
						pipe_connect->data_fifo_size);
		data_buf->phys_base = page_to_phys(sg_page(data_sgt.sgl));
		sg_free_table(&data_sgt);
		log_event_dbg("%s: data_buf:%s virt:%pK, phys:%lx, iova:%lx\n",
			__func__, dev_name(dev), data_buf->base,
			(unsigned long)data_buf->phys_base, data_buf->iova);

		desc_buf->size = pipe_connect->desc_fifo_size;
		desc_buf->base = dma_alloc_attrs(dev,
				pipe_connect->desc_fifo_size,
				&desc_iova, GFP_KERNEL,
				DMA_ATTR_FORCE_CONTIGUOUS);
		if (!desc_buf->base) {
			log_event_err("%s: desc_fifo: dma_alloc_attr failed\n",
								__func__);
			dma_free_attrs(dev, pipe_connect->data_fifo_size,
				data_buf->base, data_buf->iova,
				DMA_ATTR_FORCE_CONTIGUOUS);
			ret = -ENOMEM;
			goto err_exit;
		}
		memset(desc_buf->base, 0, pipe_connect->desc_fifo_size);
		desc_buf->iova = desc_iova;
		dma_get_sgtable(dev, &desc_sgt, desc_buf->base, desc_buf->iova,
								desc_buf->size);
		desc_buf->phys_base = page_to_phys(sg_page(desc_sgt.sgl));
		sg_free_table(&desc_sgt);
		log_event_dbg("%s: desc_buf:%s virt:%pK, phys:%lx, iova:%lx\n",
			__func__, dev_name(dev), desc_buf->base,
			(unsigned long)desc_buf->phys_base, desc_buf->iova);
		break;
	default:
		log_event_err("%s: invalid mem type\n", __func__);
		ret = -EINVAL;
	}

	return ret;

err_exit:
	return ret;
}

int usb_bam_alloc_fifos(enum usb_ctrl cur_bam, u8 idx)
{
	int ret;
	struct usb_bam_ctx_type *ctx = &msm_usb_bam[cur_bam];
	struct usb_bam_pipe_connect *pipe_connect =
					&ctx->usb_bam_connections[idx];

	ret = usb_bam_alloc_buffer(pipe_connect);
	if (ret) {
		log_event_err("%s(): Error(%d) allocating buffer\n",
				__func__, ret);
		return ret;
	}
	return 0;
}
EXPORT_SYMBOL(usb_bam_alloc_fifos);

int usb_bam_free_fifos(enum usb_ctrl cur_bam, u8 idx)
{
	struct usb_bam_ctx_type *ctx = &msm_usb_bam[cur_bam];
	struct usb_bam_pipe_connect *pipe_connect =
				&ctx->usb_bam_connections[idx];
	struct sps_connect *sps_connection =
				&ctx->usb_bam_sps.sps_connections[idx];
	struct device *dev = &ctx->usb_bam_pdev->dev;
	u32 data_fifo_size;

	pr_debug("%s(): data size:%x desc size:%x\n",
			__func__, sps_connection->data.size,
			sps_connection->desc.size);

	if (dev->parent)
		dev = dev->parent;

	switch (pipe_connect->mem_type) {
	case SYSTEM_MEM:
		log_event_dbg("%s: Freeing system memory used by PIPE\n",
				__func__);
		if (sps_connection->data.iova) {
			data_fifo_size = sps_connection->data.size;
			dma_free_attrs(dev, data_fifo_size,
					sps_connection->data.base,
					sps_connection->data.iova,
					DMA_ATTR_FORCE_CONTIGUOUS);
			sps_connection->data.iova = 0;
			sps_connection->data.phys_base = 0;
			pipe_connect->data_mem_buf.base = NULL;
		}
		if (sps_connection->desc.iova) {
			dma_free_attrs(dev, sps_connection->desc.size,
					sps_connection->desc.base,
					sps_connection->desc.iova,
					DMA_ATTR_FORCE_CONTIGUOUS);
			sps_connection->desc.iova = 0;
			sps_connection->desc.phys_base = 0;
			pipe_connect->desc_mem_buf.base = NULL;
		}
		break;
	case OCI_MEM:
		log_event_dbg("Freeing oci memory used by BAM PIPE\n");
		if (sps_connection->data.base) {
			if (sps_connection->data.iova) {
				dma_unmap_resource(dev,
					sps_connection->data.iova,
					sps_connection->data.size,
					DMA_BIDIRECTIONAL, 0);
				sps_connection->data.iova = 0;
			}
			iounmap(sps_connection->data.base);
			sps_connection->data.base = NULL;
			pipe_connect->data_mem_buf.base = NULL;
		}
		if (sps_connection->desc.base) {
			if (sps_connection->desc.iova) {
				dma_unmap_resource(dev,
					sps_connection->desc.iova,
					sps_connection->desc.size,
					DMA_BIDIRECTIONAL, 0);
				sps_connection->desc.iova = 0;
			}
			iounmap(sps_connection->desc.base);
			sps_connection->desc.base = NULL;
			pipe_connect->desc_mem_buf.base = NULL;
		}
		break;
	case SPS_PIPE_MEM:
		log_event_dbg("%s: nothing to be be\n", __func__);
		break;
	}

	return 0;
}
EXPORT_SYMBOL(usb_bam_free_fifos);

static int connect_pipe(enum usb_ctrl cur_bam, u8 idx, u32 *usb_pipe_idx,
							unsigned long iova)
{
	int ret;
	struct usb_bam_ctx_type *ctx = &msm_usb_bam[cur_bam];
	struct usb_bam_sps_type usb_bam_sps = ctx->usb_bam_sps;
	struct sps_pipe **pipe = &(usb_bam_sps.sps_pipes[idx]);
	struct sps_connect *sps_connection = &usb_bam_sps.sps_connections[idx];
	struct usb_bam_pipe_connect *pipe_connect =
					&ctx->usb_bam_connections[idx];
	enum usb_bam_pipe_dir dir = pipe_connect->dir;
	struct sps_mem_buffer *data_buf = &(pipe_connect->data_mem_buf);
	struct sps_mem_buffer *desc_buf = &(pipe_connect->desc_mem_buf);

	*pipe = sps_alloc_endpoint();
	if (*pipe == NULL) {
		log_event_err("%s: sps_alloc_endpoint failed\n", __func__);
		return -ENOMEM;
	}

	ret = sps_get_config(*pipe, sps_connection);
	if (ret) {
		log_event_err("%s: tx get config failed %d\n", __func__, ret);
		goto free_sps_endpoint;
	}

	ret = sps_phy2h(pipe_connect->src_phy_addr, &(sps_connection->source));
	if (ret) {
		log_event_err("%s: sps_phy2h failed (src BAM) %d\n",
				__func__, ret);
		goto free_sps_endpoint;
	}

	sps_connection->src_pipe_index = pipe_connect->src_pipe_index;
	ret = sps_phy2h(pipe_connect->dst_phy_addr,
		&(sps_connection->destination));
	if (ret) {
		log_event_err("%s: sps_phy2h failed (dst BAM) %d\n",
				__func__, ret);
		goto free_sps_endpoint;
	}
	sps_connection->dest_pipe_index = pipe_connect->dst_pipe_index;

	if (dir == USB_TO_PEER_PERIPHERAL) {
		sps_connection->mode = SPS_MODE_SRC;
		*usb_pipe_idx = pipe_connect->src_pipe_index;
		sps_connection->dest_iova = iova;
	} else {
		sps_connection->mode = SPS_MODE_DEST;
		*usb_pipe_idx = pipe_connect->dst_pipe_index;
		sps_connection->source_iova = iova;
	}

	sps_connection->data = *data_buf;
	sps_connection->desc = *desc_buf;
	sps_connection->event_thresh = 16;
	sps_connection->options = SPS_O_AUTO_ENABLE;

	ret = sps_connect(*pipe, sps_connection);
	if (ret < 0) {
		log_event_err("%s: sps_connect failed %d\n", __func__, ret);
		goto error;
	}

	return 0;

error:
	sps_disconnect(*pipe);
free_sps_endpoint:
	sps_free_endpoint(*pipe);
	*pipe = NULL;
	return ret;
}

static int disconnect_pipe(enum usb_ctrl cur_bam, u8 idx)
{
	struct usb_bam_ctx_type *ctx = &msm_usb_bam[cur_bam];
	struct sps_pipe *pipe = ctx->usb_bam_sps.sps_pipes[idx];
	struct sps_connect *sps_connection =
				&ctx->usb_bam_sps.sps_connections[idx];

	sps_disconnect(pipe);
	sps_free_endpoint(pipe);
	ctx->usb_bam_sps.sps_pipes[idx] = NULL;
	sps_connection->options &= ~SPS_O_AUTO_ENABLE;

	return 0;
}

static bool _hsic_host_bam_resume_core(void)
{
	/* Exit from "full suspend" in case of hsic host */
	if (host_info[HSIC_CTRL].dev) {
		log_event_dbg("%s: Getting hsic device %pK\n", __func__,
			host_info[HSIC_CTRL].dev);
		pm_runtime_get(host_info[HSIC_CTRL].dev);
		return true;
	}
	return false;
}

static bool usb_bam_resume_core(enum usb_ctrl bam_type,
	enum usb_bam_mode bam_mode)
{
	log_event_dbg("%s: enter bam=%s\n", __func__,
			bam_enable_strings[bam_type]);

	if ((bam_mode == USB_BAM_DEVICE) || (bam_type != HSIC_CTRL)) {
		log_event_err("%s: Invalid BAM type %d\n", __func__, bam_type);
		return false;
	}

	return _hsic_host_bam_resume_core();
}

static void _msm_bam_wait_for_host_prod_granted(enum usb_ctrl bam_type)
{
	struct usb_bam_ctx_type *ctx = &msm_usb_bam[bam_type];

	spin_lock(&ctx->usb_bam_lock);

	log_event_dbg("%s: enter bam=%s\n", __func__,
			bam_enable_strings[bam_type]);
	ctx->is_bam_inactivity = false;

	/* Get back to resume state including wakeup ipa */
	usb_bam_resume_core(bam_type, USB_BAM_HOST);

	spin_unlock(&ctx->usb_bam_lock);

}

void msm_bam_wait_for_hsic_host_prod_granted(void)
{
	_msm_bam_wait_for_host_prod_granted(HSIC_CTRL);
}

int get_qdss_bam_info(enum usb_ctrl cur_bam, u8 idx,
			phys_addr_t *p_addr, u32 *bam_size)
{
	int ret = 0;
	struct usb_bam_ctx_type *ctx = &msm_usb_bam[cur_bam];
	struct usb_bam_pipe_connect *pipe_connect =
				&ctx->usb_bam_connections[idx];
	unsigned long peer_bam_handle;

	ret = sps_phy2h(pipe_connect->src_phy_addr, &peer_bam_handle);
	if (ret) {
		log_event_err("%s: sps_phy2h failed (src BAM) %d\n",
						__func__, ret);
		return ret;
	}

	ret = sps_get_bam_addr(peer_bam_handle, p_addr, bam_size);
	if (ret) {
		log_event_err("%s: sps_get_bam_addr failed%d\n",
						__func__, ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(get_qdss_bam_info);

int usb_bam_connect(enum usb_ctrl cur_bam, int idx, u32 *bam_pipe_idx,
						unsigned long iova)
{
	int ret;
	struct usb_bam_ctx_type *ctx = &msm_usb_bam[cur_bam];
	struct usb_bam_pipe_connect *pipe_connect =
				&ctx->usb_bam_connections[idx];
	struct device *bam_dev = &ctx->usb_bam_pdev->dev;

	if (pipe_connect->enabled) {
		pr_warn("%s: connection %d was already established\n",
				__func__, idx);
		return 0;
	}

	if (!bam_pipe_idx) {
		log_event_err("%s: invalid bam_pipe_idx\n", __func__);
		return -EINVAL;
	}
	if (idx < 0 || idx > ctx->max_connections) {
		log_event_err("idx is wrong %d\n", idx);
		return -EINVAL;
	}

	log_event_dbg("%s: PM Runtime GET %d, count: %d\n",
			__func__, idx, get_pm_runtime_counter(bam_dev));
	pm_runtime_get_sync(bam_dev);

	spin_lock(&ctx->usb_bam_lock);
	/* Check if BAM requires RESET before connect and reset of first pipe */
	if ((ctx->usb_bam_data->reset_on_connect) &&
			    (ctx->pipes_enabled_per_bam == 0)) {
		spin_unlock(&ctx->usb_bam_lock);
		sps_device_reset(ctx->h_bam);
		spin_lock(&ctx->usb_bam_lock);
	}
	spin_unlock(&ctx->usb_bam_lock);

	ret = connect_pipe(cur_bam, idx, bam_pipe_idx, iova);
	if (ret) {
		log_event_err("%s: pipe connection[%d] failure\n",
				__func__, idx);
		log_event_dbg("%s: err, PM RT PUT %d, count: %d\n",
			__func__, idx, get_pm_runtime_counter(bam_dev));
		pm_runtime_put_sync(bam_dev);
		return ret;
	}
	log_event_dbg("%s: pipe connection[%d] success\n", __func__, idx);
	pipe_connect->enabled = true;
	spin_lock(&ctx->usb_bam_lock);
	ctx->pipes_enabled_per_bam += 1;
	spin_unlock(&ctx->usb_bam_lock);

	return 0;
}
EXPORT_SYMBOL(usb_bam_connect);

int usb_bam_get_pipe_type(enum usb_ctrl bam_type, u8 idx,
			  enum usb_bam_pipe_type *type)
{
	struct usb_bam_ctx_type *ctx = &msm_usb_bam[bam_type];
	struct usb_bam_pipe_connect *pipe_connect =
			 &ctx->usb_bam_connections[idx];

	if (idx >= ctx->max_connections) {
		log_event_err("%s: Invalid connection index\n", __func__);
		return -EINVAL;
	}

	if (!type) {
		log_event_err("%s: null pointer provided for type\n", __func__);
		return -EINVAL;
	}

	*type = pipe_connect->pipe_type;
	return 0;
}
EXPORT_SYMBOL(usb_bam_get_pipe_type);

static void usb_bam_work(struct work_struct *w)
{
	int i;
	struct usb_bam_event_info *event_info =
		container_of(w, struct usb_bam_event_info, event_w);
	struct usb_bam_pipe_connect *pipe_connect =
		container_of(event_info, struct usb_bam_pipe_connect, event);
	struct usb_bam_ctx_type *ctx = &msm_usb_bam[pipe_connect->bam_type];
	struct usb_bam_pipe_connect *pipe_iter;
	int (*callback)(void *priv);
	void *param = NULL;

	switch (event_info->type) {
	case USB_BAM_EVENT_WAKEUP:
	case USB_BAM_EVENT_WAKEUP_PIPE:

		log_event_dbg("%s received USB_BAM_EVENT_WAKEUP\n", __func__);

		/* Notify about wakeup / activity of the bam */
		if (event_info->callback)
			event_info->callback(event_info->param);

		/*
		 * Reset inactivity timer counter if this pipe's bam
		 * has inactivity timeout.
		 */
		spin_lock(&ctx->usb_bam_lock);
		if (ctx->inactivity_timer_ms)
			usb_bam_set_inactivity_timer(pipe_connect->bam_type);
		spin_unlock(&ctx->usb_bam_lock);

		break;

	case USB_BAM_EVENT_INACTIVITY:

		log_event_dbg("%s received USB_BAM_EVENT_INACTIVITY\n",
				__func__);

		/*
		 * Since event info is one structure per pipe, it might be
		 * overridden when we will register the wakeup events below,
		 * and still we want ot register the wakeup events before we
		 * notify on the inactivity in order to identify the next
		 * activity as soon as possible.
		 */
		callback = event_info->callback;
		param = event_info->param;

		/*
		 * Upon inactivity, configure wakeup irq for all pipes
		 * that are into the usb bam.
		 */
		spin_lock(&ctx->usb_bam_lock);
		for (i = 0; i < ctx->max_connections; i++) {
			pipe_iter = &ctx->usb_bam_connections[i];
			if (pipe_iter->bam_type == pipe_connect->bam_type &&
			    pipe_iter->dir == PEER_PERIPHERAL_TO_USB &&
			    pipe_iter->enabled) {
				log_event_dbg("%s: Register wakeup on pipe %pK\n",
					__func__, pipe_iter);
				__usb_bam_register_wake_cb(
					pipe_connect->bam_type, i,
					pipe_iter->activity_notify,
					pipe_iter->priv,
					false);
			}
		}
		spin_unlock(&ctx->usb_bam_lock);

		/* Notify about the inactivity to the USB class driver */
		if (callback)
			callback(param);

		break;
	default:
		log_event_err("%s: unknown usb bam event type %d\n", __func__,
			event_info->type);
	}
}

static void usb_bam_wake_cb(struct sps_event_notify *notify)
{
	struct usb_bam_event_info *event_info =
		(struct usb_bam_event_info *)notify->user;
	struct usb_bam_pipe_connect *pipe_connect =
		container_of(event_info,
			     struct usb_bam_pipe_connect,
			     event);
	enum usb_ctrl bam = pipe_connect->bam_type;
	struct usb_bam_ctx_type *ctx = &msm_usb_bam[bam];

	spin_lock(&ctx->usb_bam_lock);

	if (event_info->type == USB_BAM_EVENT_WAKEUP_PIPE)
		queue_work(ctx->usb_bam_wq, &event_info->event_w);
	else if (event_info->type == USB_BAM_EVENT_WAKEUP &&
			ctx->is_bam_inactivity) {

		/*
		 * Sps wake event is per pipe, so usb_bam_wake_cb is
		 * called per pipe. However, we want to filter the wake
		 * event to be wake event per all the pipes.
		 * Therefore, the first pipe that awaked will be considered
		 * as global bam wake event.
		 */
		ctx->is_bam_inactivity = false;

		queue_work(ctx->usb_bam_wq, &event_info->event_w);
	}

	spin_unlock(&ctx->usb_bam_lock);
}

static int __usb_bam_register_wake_cb(enum usb_ctrl bam_type, int idx,
				int (*callback)(void *user), void *param,
				bool trigger_cb_per_pipe)
{
	struct sps_pipe *pipe;
	struct sps_connect *sps_connection;
	struct usb_bam_pipe_connect *pipe_connect;
	struct usb_bam_event_info *wake_event_info;
	struct usb_bam_ctx_type *ctx = &msm_usb_bam[bam_type];
	int ret;

	if (idx < 0 || idx > ctx->max_connections) {
		log_event_err("%s:idx is wrong %d\n", __func__, idx);
		return -EINVAL;
	}
	pipe = ctx->usb_bam_sps.sps_pipes[idx];
	sps_connection = &ctx->usb_bam_sps.sps_connections[idx];
	pipe_connect = &ctx->usb_bam_connections[idx];
	wake_event_info = &pipe_connect->event;

	wake_event_info->type = (trigger_cb_per_pipe ?
				USB_BAM_EVENT_WAKEUP_PIPE :
				USB_BAM_EVENT_WAKEUP);
	wake_event_info->param = param;
	wake_event_info->callback = callback;
	wake_event_info->event.mode = SPS_TRIGGER_CALLBACK;
	wake_event_info->event.xfer_done = NULL;
	wake_event_info->event.callback = callback ? usb_bam_wake_cb : NULL;
	wake_event_info->event.user = wake_event_info;
	wake_event_info->event.options = SPS_O_WAKEUP;
	ret = sps_register_event(pipe, &wake_event_info->event);
	if (ret) {
		log_event_err("%s: sps_register_event() failed %d\n",
				__func__, ret);
		return ret;
	}

	sps_connection->options = callback ?
		(SPS_O_AUTO_ENABLE | SPS_O_WAKEUP | SPS_O_WAKEUP_IS_ONESHOT) :
		SPS_O_AUTO_ENABLE;
	ret = sps_set_config(pipe, sps_connection);
	if (ret) {
		log_event_err("%s: sps_set_config() failed %d\n",
				__func__, ret);
		return ret;
	}
	log_event_dbg("%s: success\n", __func__);
	return 0;
}

int usb_bam_register_wake_cb(enum usb_ctrl bam_type, u8 idx,
			     int (*callback)(void *user), void *param)
{
	return __usb_bam_register_wake_cb(bam_type, idx, callback, param, true);
}

int usb_bam_register_start_stop_cbs(enum usb_ctrl bam_type, u8 dst_idx,
	void (*start)(void *, enum usb_bam_pipe_dir),
	void (*stop)(void *, enum usb_bam_pipe_dir), void *param)
{
	struct usb_bam_ctx_type *ctx = &msm_usb_bam[bam_type];
	struct usb_bam_pipe_connect *pipe_connect =
				&ctx->usb_bam_connections[dst_idx];

	log_event_dbg("%s: Register for %d\n", __func__, dst_idx);
	pipe_connect->start = start;
	pipe_connect->stop = stop;
	pipe_connect->start_stop_param = param;

	return 0;
}

int usb_bam_disconnect_pipe(enum usb_ctrl bam_type, u8 idx)
{
	struct usb_bam_ctx_type *ctx = &msm_usb_bam[bam_type];
	struct usb_bam_pipe_connect *pipe_connect;
	struct device *bam_dev = &ctx->usb_bam_pdev->dev;
	int ret;

	pipe_connect = &ctx->usb_bam_connections[idx];

	if (!pipe_connect->enabled) {
		log_event_err("%s: connection %d isn't enabled\n",
			__func__, idx);
		return 0;
	}

	ret = disconnect_pipe(bam_type, idx);
	if (ret) {
		log_event_err("%s: src pipe disconnection failure\n", __func__);
		return ret;
	}

	pipe_connect->enabled = false;
	spin_lock(&ctx->usb_bam_lock);
	if (!ctx->pipes_enabled_per_bam) {
		log_event_err("%s: wrong pipes enabled counter for bam_type=%d\n",
			__func__, bam_type);
	} else {
		ctx->pipes_enabled_per_bam -= 1;
	}
	spin_unlock(&ctx->usb_bam_lock);

	log_event_dbg("%s: success disconnecting pipe %d\n", __func__, idx);

	if (ctx->usb_bam_data->reset_on_disconnect
				&& !ctx->pipes_enabled_per_bam)
		sps_device_reset(ctx->h_bam);

	/* This function is directly called by USB Transport drivers
	 * to disconnect pipes. Drop runtime usage count here.
	 */
	log_event_dbg("%s: PM Runtime PUT %d, count: %d\n", __func__, idx,
			get_pm_runtime_counter(bam_dev));
	pm_runtime_put_sync(bam_dev);

	return 0;
}
EXPORT_SYMBOL(usb_bam_disconnect_pipe);

static void usb_bam_sps_events(enum sps_callback_case sps_cb_case, void *user)
{
	int i;
	struct usb_bam_ctx_type *ctx = user;
	struct usb_bam_pipe_connect *pipe_connect;
	struct usb_bam_event_info *event_info;

	switch (sps_cb_case) {
	case SPS_CALLBACK_BAM_TIMER_IRQ:

		log_event_dbg("%s: received SPS_CALLBACK_BAM_TIMER_IRQ\n",
				__func__);

		spin_lock(&ctx->usb_bam_lock);

		ctx->is_bam_inactivity = true;

		for (i = 0; i < ctx->max_connections; i++) {
			pipe_connect = &ctx->usb_bam_connections[i];

			/*
			 * Notify inactivity once, Since it is global
			 * for all pipes on bam. Notify only if we have
			 * connected pipes.
			 */
			if (pipe_connect->enabled) {
				event_info = &pipe_connect->event;
				event_info->type = USB_BAM_EVENT_INACTIVITY;
				event_info->param = pipe_connect->priv;
				event_info->callback =
					pipe_connect->inactivity_notify;
				queue_work(ctx->usb_bam_wq,
						&event_info->event_w);
				break;
			}
		}

		spin_unlock(&ctx->usb_bam_lock);

		break;
	default:
		log_event_dbg("%s: received sps_cb_case=%d\n", __func__,
			(int)sps_cb_case);
	}
}

static struct msm_usb_bam_data *usb_bam_dt_to_data(
	struct platform_device *pdev, u32 usb_addr)
{
	struct msm_usb_bam_data *usb_bam_data;
	struct device_node *node = pdev->dev.of_node;
	int rc = 0;
	u8 i = 0;
	u32 bam = USB_CTRL_UNUSED;
	u32 addr = 0;
	u32 threshold, max_connections = 0;
	static struct usb_bam_pipe_connect *usb_bam_connections;

	usb_bam_data = devm_kzalloc(&pdev->dev, sizeof(*usb_bam_data),
								GFP_KERNEL);
	if (!usb_bam_data)
		return NULL;

	rc = of_property_read_u32(node, "qcom,bam-type", &bam);
	if (rc) {
		log_event_err("%s: bam type is missing in device tree\n",
			__func__);
		return NULL;
	}
	if (bam >= MAX_BAMS) {
		log_event_err("%s: Invalid bam type %d in device tree\n",
			__func__, bam);
		return NULL;
	}

	usb_bam_data->bam_type = bam;

	usb_bam_data->reset_on_connect = of_property_read_bool(node,
					"qcom,reset-bam-on-connect");

	usb_bam_data->reset_on_disconnect = of_property_read_bool(node,
					"qcom,reset-bam-on-disconnect");

	rc = of_property_read_u32(node, "qcom,usb-bam-num-pipes",
		&usb_bam_data->usb_bam_num_pipes);
	if (rc) {
		log_event_err("Invalid usb bam num pipes property\n");
		return NULL;
	}

	rc = of_property_read_u32(node, "qcom,usb-bam-max-mbps-highspeed",
		&usb_bam_data->max_mbps_highspeed);
	if (rc)
		usb_bam_data->max_mbps_highspeed = 0;

	rc = of_property_read_u32(node, "qcom,usb-bam-max-mbps-superspeed",
		&usb_bam_data->max_mbps_superspeed);
	if (rc)
		usb_bam_data->max_mbps_superspeed = 0;

	rc = of_property_read_u32(node, "qcom,usb-bam-fifo-baseaddr", &addr);
	if (rc)
		pr_debug("%s: Invalid usb base address property\n", __func__);
	else
		usb_bam_data->usb_bam_fifo_baseaddr = addr;

	usb_bam_data->disable_clk_gating = of_property_read_bool(node,
		"qcom,disable-clk-gating");

	rc = of_property_read_u32(node, "qcom,usb-bam-override-threshold",
			&threshold);
	if (rc)
		usb_bam_data->override_threshold = USB_THRESHOLD;
	else
		usb_bam_data->override_threshold = threshold;

	for_each_child_of_node(pdev->dev.of_node, node)
		max_connections++;

	if (!max_connections) {
		log_event_err("%s: error: max_connections is zero\n", __func__);
		goto err;
	}

	usb_bam_connections = devm_kzalloc(&pdev->dev, max_connections *
		sizeof(struct usb_bam_pipe_connect), GFP_KERNEL);

	if (!usb_bam_connections) {
		log_event_err("%s: devm_kzalloc failed(%d)\n",
				__func__,  __LINE__);
		return NULL;
	}

	/* retrieve device tree parameters */
	for_each_child_of_node(pdev->dev.of_node, node) {
		usb_bam_connections[i].bam_type = bam;

		rc = of_property_read_string(node, "label",
			&usb_bam_connections[i].name);
		if (rc)
			goto err;

		rc = of_property_read_u32(node, "qcom,usb-bam-mem-type",
			&usb_bam_connections[i].mem_type);
		if (rc)
			goto err;

		if (usb_bam_connections[i].mem_type == OCI_MEM) {
			if (!usb_bam_data->usb_bam_fifo_baseaddr) {
				log_event_err("%s: base address is missing\n",
					__func__);
				goto err;
			}
		}
		rc = of_property_read_u32(node, "qcom,peer-bam",
			&usb_bam_connections[i].peer_bam);
		if (rc) {
			log_event_err("%s: peer bam is missing in device tree\n",
				__func__);
			goto err;
		}
		/*
		 * Store USB bam_type to be used with QDSS. As only one device
		 * bam is currently supported, check the same in DT connections
		 */
		if (usb_bam_connections[i].peer_bam == QDSS_P_BAM) {
			if (qdss_usb_bam_type) {
				log_event_err("%s: overriding QDSS pipe!, update DT\n",
					__func__);
			}
			qdss_usb_bam_type = usb_bam_connections[i].bam_type;
		}

		rc = of_property_read_u32(node, "qcom,dir",
			&usb_bam_connections[i].dir);
		if (rc) {
			log_event_err("%s: direction is missing in device tree\n",
				__func__);
			goto err;
		}

		rc = of_property_read_u32(node, "qcom,pipe-num",
			&usb_bam_connections[i].pipe_num);
		if (rc) {
			log_event_err("%s: pipe num is missing in device tree\n",
				__func__);
			goto err;
		}

		rc = of_property_read_u32(node, "qcom,pipe-connection-type",
			&usb_bam_connections[i].pipe_type);
		if (rc)
			pr_debug("%s: pipe type is defaulting to bam2bam\n",
					__func__);

		of_property_read_u32(node, "qcom,peer-bam-physical-address",
						&addr);
		if (usb_bam_connections[i].dir == USB_TO_PEER_PERIPHERAL) {
			usb_bam_connections[i].src_phy_addr = usb_addr;
			usb_bam_connections[i].dst_phy_addr = addr;
		} else {
			usb_bam_connections[i].src_phy_addr = addr;
			usb_bam_connections[i].dst_phy_addr = usb_addr;
		}

		of_property_read_u32(node, "qcom,src-bam-pipe-index",
			&usb_bam_connections[i].src_pipe_index);

		of_property_read_u32(node, "qcom,dst-bam-pipe-index",
			&usb_bam_connections[i].dst_pipe_index);

		of_property_read_u32(node, "qcom,data-fifo-offset",
			&usb_bam_connections[i].data_fifo_base_offset);

		rc = of_property_read_u32(node, "qcom,data-fifo-size",
			&usb_bam_connections[i].data_fifo_size);
		if (rc)
			goto err;

		of_property_read_u32(node, "qcom,descriptor-fifo-offset",
			&usb_bam_connections[i].desc_fifo_base_offset);

		rc = of_property_read_u32(node, "qcom,descriptor-fifo-size",
			&usb_bam_connections[i].desc_fifo_size);
		if (rc)
			goto err;
		i++;
	}

	msm_usb_bam[bam].usb_bam_connections = usb_bam_connections;
	msm_usb_bam[bam].max_connections = max_connections;

	return usb_bam_data;
err:
	log_event_err("%s: failed\n", __func__);
	return NULL;
}

static void msm_usb_bam_update_props(struct sps_bam_props *props,
				struct platform_device *pdev)
{
	struct usb_bam_ctx_type *ctx = dev_get_drvdata(&pdev->dev);
	enum usb_ctrl bam_type = ctx->usb_bam_data->bam_type;
	struct device *dev;

	props->phys_addr = ctx->io_res->start;
	props->virt_addr = NULL;
	props->virt_size = resource_size(ctx->io_res);
	props->irq = ctx->irq;
	props->summing_threshold = ctx->usb_bam_data->override_threshold;
	props->event_threshold = ctx->usb_bam_data->override_threshold;
	props->num_pipes = ctx->usb_bam_data->usb_bam_num_pipes;
	props->callback = usb_bam_sps_events;
	props->user = bam_enable_strings[bam_type];

	/*
	 * HSUSB and HSIC Cores don't support RESET ACK signal to BAMs
	 * Hence, let BAM to ignore acknowledge from USB while resetting PIPE
	 */
	if (ctx->usb_bam_data->ignore_core_reset_ack && bam_type != DWC3_CTRL)
		props->options = SPS_BAM_NO_EXT_P_RST;

	if (ctx->usb_bam_data->disable_clk_gating)
		props->options |= SPS_BAM_NO_LOCAL_CLK_GATING;

	/*
	 * HSUSB BAM is not NDP BAM and it must be enabled before
	 * starting peripheral controller to avoid switching USB core mode
	 * from legacy to BAM with ongoing data transfers.
	 */
	if (bam_type == CI_CTRL) {
		log_event_dbg("Register and enable HSUSB BAM\n");
		props->options |= SPS_BAM_OPT_ENABLE_AT_BOOT;
		props->options |= SPS_BAM_FORCE_RESET;
	}

	dev = &ctx->usb_bam_pdev->dev;
	if (dev && dev->parent && device_property_present(dev->parent, "iommus")
		&& !device_property_present(dev->parent,
						"qcom,smmu-s1-bypass")) {
		pr_info("%s: setting SPS_BAM_SMMU_EN flag with (%s)\n",
						__func__, dev_name(dev));
		props->options |= SPS_BAM_SMMU_EN;
	}
}

static int usb_bam_init(struct platform_device *pdev)
{
	int ret;
	struct usb_bam_ctx_type *ctx = dev_get_drvdata(&pdev->dev);
	enum usb_ctrl bam_type = ctx->usb_bam_data->bam_type;
	struct sps_bam_props props;
	struct device *dev;

	memset(&props, 0, sizeof(props));

	props.phys_addr = ctx->io_res->start;
	props.virt_size = resource_size(ctx->io_res);
	props.irq = ctx->irq;
	props.summing_threshold = ctx->usb_bam_data->override_threshold;
	props.event_threshold = ctx->usb_bam_data->override_threshold;
	props.num_pipes = ctx->usb_bam_data->usb_bam_num_pipes;
	props.callback = usb_bam_sps_events;
	props.user = &msm_usb_bam[bam_type];

	if (ctx->usb_bam_data->disable_clk_gating)
		props.options |= SPS_BAM_NO_LOCAL_CLK_GATING;

	dev = &ctx->usb_bam_pdev->dev;
	if (dev && dev->parent && device_property_present(dev->parent, "iommus")
		&& !device_property_present(dev->parent,
						"qcom,smmu-s1-bypass")) {
		pr_info("%s: setting SPS_BAM_SMMU_EN flag with (%s)\n",
						__func__, dev_name(dev));
		props.options |= SPS_BAM_SMMU_EN;
	}

	ret = sps_register_bam_device(&props, &ctx->h_bam);
	if (ret < 0) {
		log_event_err("%s: register bam error %d\n", __func__, ret);
		return -EFAULT;
	}

	return 0;
}

static int enable_usb_bam(struct platform_device *pdev)
{
	int ret;
	struct usb_bam_ctx_type *ctx = dev_get_drvdata(&pdev->dev);

	ret = usb_bam_init(pdev);
	if (ret)
		return ret;

	ctx->usb_bam_sps.sps_pipes = devm_kzalloc(&pdev->dev,
		ctx->max_connections * sizeof(struct sps_pipe *),
		GFP_KERNEL);

	if (!ctx->usb_bam_sps.sps_pipes) {
		log_event_err("%s: failed to allocate sps_pipes\n", __func__);
		return -ENOMEM;
	}

	ctx->usb_bam_sps.sps_connections = devm_kzalloc(&pdev->dev,
		ctx->max_connections * sizeof(struct sps_connect),
		GFP_KERNEL);
	if (!ctx->usb_bam_sps.sps_connections) {
		log_event_err("%s: failed to allocate sps_connections\n",
				__func__);
		return -ENOMEM;
	}

	return 0;
}

static int usb_bam_panic_notifier(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	int i;
	struct usb_bam_ctx_type *ctx;

	for (i = 0; i < MAX_BAMS; i++) {
		ctx = &msm_usb_bam[i];
		if (ctx->h_bam)
			break;
	}

	if (i == MAX_BAMS)
		goto fail;

	if (!ctx->pipes_enabled_per_bam)
		goto fail;

	pr_err("%s: dump usb bam registers here in call back!\n",
								__func__);
	sps_get_bam_debug_info(ctx->h_bam, 93,
			(SPS_BAM_PIPE(0) | SPS_BAM_PIPE(1)), 0, 2);

fail:
	return NOTIFY_DONE;
}

static struct notifier_block usb_bam_panic_blk = {
	.notifier_call  = usb_bam_panic_notifier,
};

void usb_bam_register_panic_hdlr(void)
{
	atomic_notifier_chain_register(&panic_notifier_list,
			&usb_bam_panic_blk);
}

static void usb_bam_unregister_panic_hdlr(void)
{
	atomic_notifier_chain_unregister(&panic_notifier_list,
			&usb_bam_panic_blk);
}

static int usb_bam_probe(struct platform_device *pdev)
{
	int ret, i, irq;
	struct resource *io_res;
	enum usb_ctrl bam_type;
	struct usb_bam_ctx_type *ctx;
	struct msm_usb_bam_data *usb_bam_data;

	io_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!io_res) {
		dev_err(&pdev->dev, "missing BAM memory resource\n");
		return -ENODEV;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "Unable to get IRQ resource\n");
		return irq;
	}

	/* specify BAM physical address to be filled in BAM connections */
	usb_bam_data = usb_bam_dt_to_data(pdev, io_res->start);
	if (!usb_bam_data)
		return -EINVAL;

	bam_type = usb_bam_data->bam_type;
	ctx = &msm_usb_bam[bam_type];
	dev_set_drvdata(&pdev->dev, ctx);

	ctx->usb_bam_pdev = pdev;
	ctx->irq = irq;
	ctx->io_res = io_res;
	ctx->usb_bam_data = usb_bam_data;

	for (i = 0; i < ctx->max_connections; i++) {
		ctx->usb_bam_connections[i].enabled = false;
		INIT_WORK(&ctx->usb_bam_connections[i].event.event_w,
			usb_bam_work);
	}

	ctx->usb_bam_wq = alloc_workqueue("usb_bam_wq",
		WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (!ctx->usb_bam_wq) {
		log_event_err("unable to create workqueue usb_bam_wq\n");
		return -ENOMEM;
	}

	ret = enable_usb_bam(pdev);
	if (ret) {
		destroy_workqueue(ctx->usb_bam_wq);
		return ret;
	}

	pm_runtime_no_callbacks(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	spin_lock_init(&ctx->usb_bam_lock);

	usb_bam_register_panic_hdlr();
	return ret;
}

int get_bam2bam_connection_info(enum usb_ctrl bam_type, u8 idx,
	u32 *usb_bam_pipe_idx, struct sps_mem_buffer *desc_fifo,
	struct sps_mem_buffer *data_fifo, enum usb_pipe_mem_type *mem_type)
{
	struct usb_bam_ctx_type *ctx = &msm_usb_bam[bam_type];
	struct usb_bam_pipe_connect *pipe_connect =
			&ctx->usb_bam_connections[idx];
	enum usb_bam_pipe_dir dir = pipe_connect->dir;

	if (dir == USB_TO_PEER_PERIPHERAL)
		*usb_bam_pipe_idx = pipe_connect->src_pipe_index;
	else
		*usb_bam_pipe_idx = pipe_connect->dst_pipe_index;

	if (data_fifo)
		memcpy(data_fifo, &pipe_connect->data_mem_buf,
		sizeof(struct sps_mem_buffer));
	if (desc_fifo)
		memcpy(desc_fifo, &pipe_connect->desc_mem_buf,
		sizeof(struct sps_mem_buffer));
	if (mem_type)
		*mem_type = pipe_connect->mem_type;

	return 0;
}
EXPORT_SYMBOL(get_bam2bam_connection_info);

int get_qdss_bam_connection_info(unsigned long *usb_bam_handle,
	u32 *usb_bam_pipe_idx, u32 *peer_pipe_idx,
	struct sps_mem_buffer *desc_fifo, struct sps_mem_buffer *data_fifo,
	enum usb_pipe_mem_type *mem_type)
{
	u8 idx;
	struct usb_bam_ctx_type *ctx = &msm_usb_bam[qdss_usb_bam_type];
	struct sps_connect *sps_connection;

	/* QDSS uses only one pipe */
	idx = usb_bam_get_connection_idx(qdss_usb_bam_type, QDSS_P_BAM,
		PEER_PERIPHERAL_TO_USB, 0);

	get_bam2bam_connection_info(qdss_usb_bam_type, idx, usb_bam_pipe_idx,
						desc_fifo, data_fifo, mem_type);


	sps_connection = &ctx->usb_bam_sps.sps_connections[idx];
	*usb_bam_handle = sps_connection->destination;
	*peer_pipe_idx = sps_connection->src_pipe_index;

	return 0;
}
EXPORT_SYMBOL(get_qdss_bam_connection_info);

int usb_bam_get_connection_idx(enum usb_ctrl bam_type, enum peer_bam client,
	enum usb_bam_pipe_dir dir, u32 num)
{
	struct usb_bam_ctx_type *ctx = &msm_usb_bam[bam_type];
	u8 i;

	for (i = 0; i < ctx->max_connections; i++) {
		if (ctx->usb_bam_connections[i].peer_bam == client &&
		    ctx->usb_bam_connections[i].dir == dir &&
		    ctx->usb_bam_connections[i].pipe_num == num) {
			log_event_dbg("%s: index %d was found\n", __func__, i);
			return i;
		}
	}

	log_event_err("%s: failed for %d\n", __func__, bam_type);
	return -ENODEV;
}
EXPORT_SYMBOL(usb_bam_get_connection_idx);

enum usb_ctrl usb_bam_get_bam_type(const char *core_name)
{
	enum usb_ctrl bam_type = get_bam_type_from_core_name(core_name);

	if (bam_type < 0 || bam_type >= MAX_BAMS) {
		log_event_err("%s: Invalid bam, type=%d, name=%s\n",
			__func__, bam_type, core_name);
		return -EINVAL;
	}

	return bam_type;
}
EXPORT_SYMBOL(usb_bam_get_bam_type);

int msm_usb_bam_enable(enum usb_ctrl bam, bool bam_enable)
{
	struct msm_usb_bam_data *usb_bam_data;
	struct usb_bam_ctx_type *ctx = &msm_usb_bam[bam];
	static bool bam_enabled;
	int ret;

	if (!ctx->usb_bam_pdev)
		return 0;

	usb_bam_data = ctx->usb_bam_data;
	if (bam != CI_CTRL)
		return 0;

	if (bam_enabled == bam_enable) {
		log_event_dbg("%s: USB BAM is already %s\n", __func__,
				bam_enable ? "Registered" : "De-registered");
		return 0;
	}

	if (bam_enable) {
		struct sps_bam_props props;

		memset(&props, 0, sizeof(props));
		msm_usb_bam_update_props(&props, ctx->usb_bam_pdev);
		msm_hw_bam_disable(1);
		ret = sps_register_bam_device(&props, &ctx->h_bam);
		bam_enabled = true;
		if (ret < 0) {
			log_event_err("%s: register bam error %d\n",
					__func__, ret);
			return -EFAULT;
		}
		log_event_dbg("%s: USB BAM Registered\n", __func__);
		msm_hw_bam_disable(0);
	} else {
		msm_hw_soft_reset();
		msm_hw_bam_disable(1);
		sps_device_reset(ctx->h_bam);
		sps_deregister_bam_device(ctx->h_bam);
		log_event_dbg("%s: USB BAM De-registered\n", __func__);
		bam_enabled = false;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(msm_usb_bam_enable);

static int usb_bam_remove(struct platform_device *pdev)
{
	struct usb_bam_ctx_type *ctx = dev_get_drvdata(&pdev->dev);

	usb_bam_unregister_panic_hdlr();
	sps_deregister_bam_device(ctx->h_bam);
	destroy_workqueue(ctx->usb_bam_wq);

	return 0;
}

static const struct of_device_id usb_bam_dt_match[] = {
	{ .compatible = "qcom,usb-bam-msm",
	},
	{}
};
MODULE_DEVICE_TABLE(of, usb_bam_dt_match);

static struct platform_driver usb_bam_driver = {
	.probe = usb_bam_probe,
	.remove = usb_bam_remove,
	.driver = {
		.name	= "usb_bam",
		.of_match_table = usb_bam_dt_match,
	},
};

static int __init init(void)
{
	return platform_driver_register(&usb_bam_driver);
}
module_init(init);

static void __exit cleanup(void)
{
	platform_driver_unregister(&usb_bam_driver);
}
module_exit(cleanup);

MODULE_DESCRIPTION("MSM USB BAM DRIVER");
MODULE_LICENSE("GPL");

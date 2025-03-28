// SPDX-License-Identifier: GPL-2.0-only
/*
 * OMAP Remote Processor driver
 *
 * Copyright (C) 2011-2020 Texas Instruments Incorporated - http://www.ti.com/
 * Copyright (C) 2011 Google, Inc.
 *
 * Ohad Ben-Cohen <ohad@wizery.com>
 * Brian Swetland <swetland@google.com>
 * Fernando Guzman Lugo <fernando.lugo@ti.com>
 * Mark Grosen <mgrosen@ti.com>
 * Suman Anna <s-anna@ti.com>
 * Hari Kanigeri <h-kanigeri2@ti.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/clk/ti.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/remoteproc.h>
#include <linux/mailbox_client.h>
#include <linux/omap-iommu.h>
#include <linux/omap-mailbox.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/reset.h>
#include <clocksource/timer-ti-dm.h>

#include <linux/platform_data/dmtimer-omap.h>

#ifdef CONFIG_ARM_DMA_USE_IOMMU
#include <asm/dma-iommu.h>
#endif

#include "omap_remoteproc.h"
#include "remoteproc_internal.h"

/* default auto-suspend delay (ms) */
#define DEFAULT_AUTOSUSPEND_DELAY		10000

/**
 * struct omap_rproc_boot_data - boot data structure for the DSP omap rprocs
 * @syscon: regmap handle for the system control configuration module
 * @boot_reg: boot register offset within the @syscon regmap
 * @boot_reg_shift: bit-field shift required for the boot address value in
 *		    @boot_reg
 */
struct omap_rproc_boot_data {
	struct regmap *syscon;
	unsigned int boot_reg;
	unsigned int boot_reg_shift;
};

/**
 * struct omap_rproc_mem - internal memory structure
 * @cpu_addr: MPU virtual address of the memory region
 * @bus_addr: bus address used to access the memory region
 * @dev_addr: device address of the memory region from DSP view
 * @size: size of the memory region
 */
struct omap_rproc_mem {
	void __iomem *cpu_addr;
	phys_addr_t bus_addr;
	u32 dev_addr;
	size_t size;
};

/**
 * struct omap_rproc_timer - data structure for a timer used by a omap rproc
 * @odt: timer pointer
 * @timer_ops: OMAP dmtimer ops for @odt timer
 * @irq: timer irq
 */
struct omap_rproc_timer {
	struct omap_dm_timer *odt;
	const struct omap_dm_timer_ops *timer_ops;
	int irq;
};

/**
 * struct omap_rproc - omap remote processor state
 * @mbox: mailbox channel handle
 * @client: mailbox client to request the mailbox channel
 * @boot_data: boot data structure for setting processor boot address
 * @mem: internal memory regions data
 * @num_mems: number of internal memory regions
 * @num_timers: number of rproc timer(s)
 * @num_wd_timers: number of rproc watchdog timers
 * @timers: timer(s) info used by rproc
 * @autosuspend_delay: auto-suspend delay value to be used for runtime pm
 * @need_resume: if true a resume is needed in the system resume callback
 * @rproc: rproc handle
 * @reset: reset handle
 * @pm_comp: completion primitive to sync for suspend response
 * @fck: functional clock for the remoteproc
 * @suspend_acked: state machine flag to store the suspend request ack
 */
struct omap_rproc {
	struct mbox_chan *mbox;
	struct mbox_client client;
	struct omap_rproc_boot_data *boot_data;
	struct omap_rproc_mem *mem;
	int num_mems;
	int num_timers;
	int num_wd_timers;
	struct omap_rproc_timer *timers;
	int autosuspend_delay;
	bool need_resume;
	struct rproc *rproc;
	struct reset_control *reset;
	struct completion pm_comp;
	struct clk *fck;
	bool suspend_acked;
};

/**
 * struct omap_rproc_mem_data - memory definitions for an omap remote processor
 * @name: name for this memory entry
 * @dev_addr: device address for the memory entry
 */
struct omap_rproc_mem_data {
	const char *name;
	const u32 dev_addr;
};

/**
 * struct omap_rproc_dev_data - device data for the omap remote processor
 * @device_name: device name of the remote processor
 * @mems: memory definitions for this remote processor
 */
struct omap_rproc_dev_data {
	const char *device_name;
	const struct omap_rproc_mem_data *mems;
};

/**
 * omap_rproc_request_timer() - request a timer for a remoteproc
 * @dev: device requesting the timer
 * @np: device node pointer to the desired timer
 * @timer: handle to a struct omap_rproc_timer to return the timer handle
 *
 * This helper function is used primarily to request a timer associated with
 * a remoteproc. The returned handle is stored in the .odt field of the
 * @timer structure passed in, and is used to invoke other timer specific
 * ops (like starting a timer either during device initialization or during
 * a resume operation, or for stopping/freeing a timer).
 *
 * Return: 0 on success, otherwise an appropriate failure
 */
static int omap_rproc_request_timer(struct device *dev, struct device_node *np,
				    struct omap_rproc_timer *timer)
{
	int ret;

	timer->odt = timer->timer_ops->request_by_node(np);
	if (!timer->odt) {
		dev_err(dev, "request for timer node %p failed\n", np);
		return -EBUSY;
	}

	ret = timer->timer_ops->set_source(timer->odt, OMAP_TIMER_SRC_SYS_CLK);
	if (ret) {
		dev_err(dev, "error setting OMAP_TIMER_SRC_SYS_CLK as source for timer node %p\n",
			np);
		timer->timer_ops->free(timer->odt);
		return ret;
	}

	/* clean counter, remoteproc code will set the value */
	timer->timer_ops->set_load(timer->odt, 0);

	return 0;
}

/**
 * omap_rproc_start_timer() - start a timer for a remoteproc
 * @timer: handle to a OMAP rproc timer
 *
 * This helper function is used to start a timer associated with a remoteproc,
 * obtained using the request_timer ops. The helper function needs to be
 * invoked by the driver to start the timer (during device initialization)
 * or to just resume the timer.
 *
 * Return: 0 on success, otherwise a failure as returned by DMTimer ops
 */
static inline int omap_rproc_start_timer(struct omap_rproc_timer *timer)
{
	return timer->timer_ops->start(timer->odt);
}

/**
 * omap_rproc_stop_timer() - stop a timer for a remoteproc
 * @timer: handle to a OMAP rproc timer
 *
 * This helper function is used to disable a timer associated with a
 * remoteproc, and needs to be called either during a device shutdown
 * or suspend operation. The separate helper function allows the driver
 * to just stop a timer without having to release the timer during a
 * suspend operation.
 *
 * Return: 0 on success, otherwise a failure as returned by DMTimer ops
 */
static inline int omap_rproc_stop_timer(struct omap_rproc_timer *timer)
{
	return timer->timer_ops->stop(timer->odt);
}

/**
 * omap_rproc_release_timer() - release a timer for a remoteproc
 * @timer: handle to a OMAP rproc timer
 *
 * This helper function is used primarily to release a timer associated
 * with a remoteproc. The dmtimer will be available for other clients to
 * use once released.
 *
 * Return: 0 on success, otherwise a failure as returned by DMTimer ops
 */
static inline int omap_rproc_release_timer(struct omap_rproc_timer *timer)
{
	return timer->timer_ops->free(timer->odt);
}

/**
 * omap_rproc_get_timer_irq() - get the irq for a timer
 * @timer: handle to a OMAP rproc timer
 *
 * This function is used to get the irq associated with a watchdog timer. The
 * function is called by the OMAP remoteproc driver to register a interrupt
 * handler to handle watchdog events on the remote processor.
 *
 * Return: irq id on success, otherwise a failure as returned by DMTimer ops
 */
static inline int omap_rproc_get_timer_irq(struct omap_rproc_timer *timer)
{
	return timer->timer_ops->get_irq(timer->odt);
}

/**
 * omap_rproc_ack_timer_irq() - acknowledge a timer irq
 * @timer: handle to a OMAP rproc timer
 *
 * This function is used to clear the irq associated with a watchdog timer.
 * The function is called by the OMAP remoteproc upon a watchdog event on the
 * remote processor to clear the interrupt status of the watchdog timer.
 */
static inline void omap_rproc_ack_timer_irq(struct omap_rproc_timer *timer)
{
	timer->timer_ops->write_status(timer->odt, OMAP_TIMER_INT_OVERFLOW);
}

/**
 * omap_rproc_watchdog_isr() - Watchdog ISR handler for remoteproc device
 * @irq: IRQ number associated with a watchdog timer
 * @data: IRQ handler data
 *
 * This ISR routine executes the required necessary low-level code to
 * acknowledge a watchdog timer interrupt. There can be multiple watchdog
 * timers associated with a rproc (like IPUs which have 2 watchdog timers,
 * one per Cortex M3/M4 core), so a lookup has to be performed to identify
 * the timer to acknowledge its interrupt.
 *
 * The function also invokes rproc_report_crash to report the watchdog event
 * to the remoteproc driver core, to trigger a recovery.
 *
 * Return: IRQ_HANDLED on success, otherwise IRQ_NONE
 */
static irqreturn_t omap_rproc_watchdog_isr(int irq, void *data)
{
	struct rproc *rproc = data;
	struct omap_rproc *oproc = rproc->priv;
	struct device *dev = rproc->dev.parent;
	struct omap_rproc_timer *timers = oproc->timers;
	struct omap_rproc_timer *wd_timer = NULL;
	int num_timers = oproc->num_timers + oproc->num_wd_timers;
	int i;

	for (i = oproc->num_timers; i < num_timers; i++) {
		if (timers[i].irq > 0 && irq == timers[i].irq) {
			wd_timer = &timers[i];
			break;
		}
	}

	if (!wd_timer) {
		dev_err(dev, "invalid timer\n");
		return IRQ_NONE;
	}

	omap_rproc_ack_timer_irq(wd_timer);

	rproc_report_crash(rproc, RPROC_WATCHDOG);

	return IRQ_HANDLED;
}

/**
 * omap_rproc_enable_timers() - enable the timers for a remoteproc
 * @rproc: handle of a remote processor
 * @configure: boolean flag used to acquire and configure the timer handle
 *
 * This function is used primarily to enable the timers associated with
 * a remoteproc. The configure flag is provided to allow the driver
 * to either acquire and start a timer (during device initialization) or
 * to just start a timer (during a resume operation).
 *
 * Return: 0 on success, otherwise an appropriate failure
 */
static int omap_rproc_enable_timers(struct rproc *rproc, bool configure)
{
	int i;
	int ret = 0;
	struct platform_device *tpdev;
	struct dmtimer_platform_data *tpdata;
	const struct omap_dm_timer_ops *timer_ops;
	struct omap_rproc *oproc = rproc->priv;
	struct omap_rproc_timer *timers = oproc->timers;
	struct device *dev = rproc->dev.parent;
	struct device_node *np = NULL;
	int num_timers = oproc->num_timers + oproc->num_wd_timers;

	if (!num_timers)
		return 0;

	if (!configure)
		goto start_timers;

	for (i = 0; i < num_timers; i++) {
		if (i < oproc->num_timers)
			np = of_parse_phandle(dev->of_node, "ti,timers", i);
		else
			np = of_parse_phandle(dev->of_node,
					      "ti,watchdog-timers",
					      (i - oproc->num_timers));
		if (!np) {
			ret = -ENXIO;
			dev_err(dev, "device node lookup for timer at index %d failed: %d\n",
				i < oproc->num_timers ? i :
				i - oproc->num_timers, ret);
			goto free_timers;
		}

		tpdev = of_find_device_by_node(np);
		if (!tpdev) {
			ret = -ENODEV;
			dev_err(dev, "could not get timer platform device\n");
			goto put_node;
		}

		tpdata = dev_get_platdata(&tpdev->dev);
		put_device(&tpdev->dev);
		if (!tpdata) {
			ret = -EINVAL;
			dev_err(dev, "dmtimer pdata structure NULL\n");
			goto put_node;
		}

		timer_ops = tpdata->timer_ops;
		if (!timer_ops || !timer_ops->request_by_node ||
		    !timer_ops->set_source || !timer_ops->set_load ||
		    !timer_ops->free || !timer_ops->start ||
		    !timer_ops->stop || !timer_ops->get_irq ||
		    !timer_ops->write_status) {
			ret = -EINVAL;
			dev_err(dev, "device does not have required timer ops\n");
			goto put_node;
		}

		timers[i].irq = -1;
		timers[i].timer_ops = timer_ops;
		ret = omap_rproc_request_timer(dev, np, &timers[i]);
		if (ret) {
			dev_err(dev, "request for timer %p failed: %d\n", np,
				ret);
			goto put_node;
		}
		of_node_put(np);

		if (i >= oproc->num_timers) {
			timers[i].irq = omap_rproc_get_timer_irq(&timers[i]);
			if (timers[i].irq < 0) {
				dev_err(dev, "get_irq for timer %p failed: %d\n",
					np, timers[i].irq);
				ret = -EBUSY;
				goto free_timers;
			}

			ret = request_irq(timers[i].irq,
					  omap_rproc_watchdog_isr, IRQF_SHARED,
					  "rproc-wdt", rproc);
			if (ret) {
				dev_err(dev, "error requesting irq for timer %p\n",
					np);
				omap_rproc_release_timer(&timers[i]);
				timers[i].odt = NULL;
				timers[i].timer_ops = NULL;
				timers[i].irq = -1;
				goto free_timers;
			}
		}
	}

start_timers:
	for (i = 0; i < num_timers; i++) {
		ret = omap_rproc_start_timer(&timers[i]);
		if (ret) {
			dev_err(dev, "start timer %p failed failed: %d\n", np,
				ret);
			break;
		}
	}
	if (ret) {
		while (i >= 0) {
			omap_rproc_stop_timer(&timers[i]);
			i--;
		}
		goto put_node;
	}
	return 0;

put_node:
	if (configure)
		of_node_put(np);
free_timers:
	while (i--) {
		if (i >= oproc->num_timers)
			free_irq(timers[i].irq, rproc);
		omap_rproc_release_timer(&timers[i]);
		timers[i].odt = NULL;
		timers[i].timer_ops = NULL;
		timers[i].irq = -1;
	}

	return ret;
}

/**
 * omap_rproc_disable_timers() - disable the timers for a remoteproc
 * @rproc: handle of a remote processor
 * @configure: boolean flag used to release the timer handle
 *
 * This function is used primarily to disable the timers associated with
 * a remoteproc. The configure flag is provided to allow the driver
 * to either stop and release a timer (during device shutdown) or to just
 * stop a timer (during a suspend operation).
 *
 * Return: 0 on success or no timers
 */
static int omap_rproc_disable_timers(struct rproc *rproc, bool configure)
{
	int i;
	struct omap_rproc *oproc = rproc->priv;
	struct omap_rproc_timer *timers = oproc->timers;
	int num_timers = oproc->num_timers + oproc->num_wd_timers;

	if (!num_timers)
		return 0;

	for (i = 0; i < num_timers; i++) {
		omap_rproc_stop_timer(&timers[i]);
		if (configure) {
			if (i >= oproc->num_timers)
				free_irq(timers[i].irq, rproc);
			omap_rproc_release_timer(&timers[i]);
			timers[i].odt = NULL;
			timers[i].timer_ops = NULL;
			timers[i].irq = -1;
		}
	}

	return 0;
}

/**
 * omap_rproc_mbox_callback() - inbound mailbox message handler
 * @client: mailbox client pointer used for requesting the mailbox channel
 * @data: mailbox payload
 *
 * This handler is invoked by omap's mailbox driver whenever a mailbox
 * message is received. Usually, the mailbox payload simply contains
 * the index of the virtqueue that is kicked by the remote processor,
 * and we let remoteproc core handle it.
 *
 * In addition to virtqueue indices, we also have some out-of-band values
 * that indicates different events. Those values are deliberately very
 * big so they don't coincide with virtqueue indices.
 */
static void omap_rproc_mbox_callback(struct mbox_client *client, void *data)
{
	struct omap_rproc *oproc = container_of(client, struct omap_rproc,
						client);
	struct device *dev = oproc->rproc->dev.parent;
	const char *name = oproc->rproc->name;
	u32 msg = (u32)data;

	dev_dbg(dev, "mbox msg: 0x%x\n", msg);

	switch (msg) {
	case RP_MBOX_CRASH:
		/*
		 * remoteproc detected an exception, notify the rproc core.
		 * The remoteproc core will handle the recovery.
		 */
		dev_err(dev, "omap rproc %s crashed\n", name);
		rproc_report_crash(oproc->rproc, RPROC_FATAL_ERROR);
		break;
	case RP_MBOX_ECHO_REPLY:
		dev_info(dev, "received echo reply from %s\n", name);
		break;
	case RP_MBOX_SUSPEND_ACK:
	case RP_MBOX_SUSPEND_CANCEL:
		oproc->suspend_acked = msg == RP_MBOX_SUSPEND_ACK;
		complete(&oproc->pm_comp);
		break;
	default:
		if (msg >= RP_MBOX_READY && msg < RP_MBOX_END_MSG)
			return;
		if (msg > oproc->rproc->max_notifyid) {
			dev_dbg(dev, "dropping unknown message 0x%x", msg);
			return;
		}
		/* msg contains the index of the triggered vring */
		if (rproc_vq_interrupt(oproc->rproc, msg) == IRQ_NONE)
			dev_dbg(dev, "no message was found in vqid %d\n", msg);
	}
}

/* kick a virtqueue */
static void omap_rproc_kick(struct rproc *rproc, int vqid)
{
	struct omap_rproc *oproc = rproc->priv;
	struct device *dev = rproc->dev.parent;
	int ret;

	/* wake up the rproc before kicking it */
	ret = pm_runtime_get_sync(dev);
	if (WARN_ON(ret < 0)) {
		dev_err(dev, "pm_runtime_get_sync() failed during kick, ret = %d\n",
			ret);
		pm_runtime_put_noidle(dev);
		return;
	}

	/* send the index of the triggered virtqueue in the mailbox payload */
	ret = mbox_send_message(oproc->mbox, (void *)vqid);
	if (ret < 0)
		dev_err(dev, "failed to send mailbox message, status = %d\n",
			ret);

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
}

/**
 * omap_rproc_write_dsp_boot_addr() - set boot address for DSP remote processor
 * @rproc: handle of a remote processor
 *
 * Set boot address for a supported DSP remote processor.
 *
 * Return: 0 on success, or -EINVAL if boot address is not aligned properly
 */
static int omap_rproc_write_dsp_boot_addr(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	struct omap_rproc *oproc = rproc->priv;
	struct omap_rproc_boot_data *bdata = oproc->boot_data;
	u32 offset = bdata->boot_reg;
	u32 value;
	u32 mask;

	if (rproc->bootaddr & (SZ_1K - 1)) {
		dev_err(dev, "invalid boot address 0x%llx, must be aligned on a 1KB boundary\n",
			rproc->bootaddr);
		return -EINVAL;
	}

	value = rproc->bootaddr >> bdata->boot_reg_shift;
	mask = ~(SZ_1K - 1) >> bdata->boot_reg_shift;

	return regmap_update_bits(bdata->syscon, offset, mask, value);
}

/*
 * Power up the remote processor.
 *
 * This function will be invoked only after the firmware for this rproc
 * was loaded, parsed successfully, and all of its resource requirements
 * were met.
 */
static int omap_rproc_start(struct rproc *rproc)
{
	struct omap_rproc *oproc = rproc->priv;
	struct device *dev = rproc->dev.parent;
	int ret;
	struct mbox_client *client = &oproc->client;

	if (oproc->boot_data) {
		ret = omap_rproc_write_dsp_boot_addr(rproc);
		if (ret)
			return ret;
	}

	client->dev = dev;
	client->tx_done = NULL;
	client->rx_callback = omap_rproc_mbox_callback;
	client->tx_block = false;
	client->knows_txdone = false;

	oproc->mbox = mbox_request_channel(client, 0);
	if (IS_ERR(oproc->mbox)) {
		ret = -EBUSY;
		dev_err(dev, "mbox_request_channel failed: %ld\n",
			PTR_ERR(oproc->mbox));
		return ret;
	}

	/*
	 * Ping the remote processor. this is only for sanity-sake;
	 * there is no functional effect whatsoever.
	 *
	 * Note that the reply will _not_ arrive immediately: this message
	 * will wait in the mailbox fifo until the remote processor is booted.
	 */
	ret = mbox_send_message(oproc->mbox, (void *)RP_MBOX_ECHO_REQUEST);
	if (ret < 0) {
		dev_err(dev, "mbox_send_message failed: %d\n", ret);
		goto put_mbox;
	}

	ret = omap_rproc_enable_timers(rproc, true);
	if (ret) {
		dev_err(dev, "omap_rproc_enable_timers failed: %d\n", ret);
		goto put_mbox;
	}

	ret = reset_control_deassert(oproc->reset);
	if (ret) {
		dev_err(dev, "reset control deassert failed: %d\n", ret);
		goto disable_timers;
	}

	/*
	 * remote processor is up, so update the runtime pm status and
	 * enable the auto-suspend. The device usage count is incremented
	 * manually for balancing it for auto-suspend
	 */
	pm_runtime_set_active(dev);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_get_noresume(dev);
	pm_runtime_enable(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;

disable_timers:
	omap_rproc_disable_timers(rproc, true);
put_mbox:
	mbox_free_channel(oproc->mbox);
	return ret;
}

/* power off the remote processor */
static int omap_rproc_stop(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	struct omap_rproc *oproc = rproc->priv;
	int ret;

	/*
	 * cancel any possible scheduled runtime suspend by incrementing
	 * the device usage count, and resuming the device. The remoteproc
	 * also needs to be woken up if suspended, to avoid the remoteproc
	 * OS to continue to remember any context that it has saved, and
	 * avoid potential issues in misindentifying a subsequent device
	 * reboot as a power restore boot
	 */
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		return ret;
	}

	ret = reset_control_assert(oproc->reset);
	if (ret)
		goto out;

	ret = omap_rproc_disable_timers(rproc, true);
	if (ret)
		goto enable_device;

	mbox_free_channel(oproc->mbox);

	/*
	 * update the runtime pm states and status now that the remoteproc
	 * has stopped
	 */
	pm_runtime_disable(dev);
	pm_runtime_dont_use_autosuspend(dev);
	pm_runtime_put_noidle(dev);
	pm_runtime_set_suspended(dev);

	return 0;

enable_device:
	reset_control_deassert(oproc->reset);
out:
	/* schedule the next auto-suspend */
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
	return ret;
}

/**
 * omap_rproc_da_to_va() - internal memory translation helper
 * @rproc: remote processor to apply the address translation for
 * @da: device address to translate
 * @len: length of the memory buffer
 *
 * Custom function implementing the rproc .da_to_va ops to provide address
 * translation (device address to kernel virtual address) for internal RAMs
 * present in a DSP or IPU device). The translated addresses can be used
 * either by the remoteproc core for loading, or by any rpmsg bus drivers.
 *
 * Return: translated virtual address in kernel memory space on success,
 *         or NULL on failure.
 */
static void *omap_rproc_da_to_va(struct rproc *rproc, u64 da, size_t len, bool *is_iomem)
{
	struct omap_rproc *oproc = rproc->priv;
	int i;
	u32 offset;

	if (len <= 0)
		return NULL;

	if (!oproc->num_mems)
		return NULL;

	for (i = 0; i < oproc->num_mems; i++) {
		if (da >= oproc->mem[i].dev_addr && da + len <=
		    oproc->mem[i].dev_addr + oproc->mem[i].size) {
			offset = da - oproc->mem[i].dev_addr;
			/* __force to make sparse happy with type conversion */
			return (__force void *)(oproc->mem[i].cpu_addr +
						offset);
		}
	}

	return NULL;
}

static const struct rproc_ops omap_rproc_ops = {
	.start		= omap_rproc_start,
	.stop		= omap_rproc_stop,
	.kick		= omap_rproc_kick,
	.da_to_va	= omap_rproc_da_to_va,
};

#ifdef CONFIG_PM
static bool _is_rproc_in_standby(struct omap_rproc *oproc)
{
	return ti_clk_is_in_standby(oproc->fck);
}

/* 1 sec is long enough time to let the remoteproc side suspend the device */
#define DEF_SUSPEND_TIMEOUT 1000
static int _omap_rproc_suspend(struct rproc *rproc, bool auto_suspend)
{
	struct device *dev = rproc->dev.parent;
	struct omap_rproc *oproc = rproc->priv;
	unsigned long to = msecs_to_jiffies(DEF_SUSPEND_TIMEOUT);
	unsigned long ta = jiffies + to;
	u32 suspend_msg = auto_suspend ?
				RP_MBOX_SUSPEND_AUTO : RP_MBOX_SUSPEND_SYSTEM;
	int ret;

	reinit_completion(&oproc->pm_comp);
	oproc->suspend_acked = false;
	ret = mbox_send_message(oproc->mbox, (void *)suspend_msg);
	if (ret < 0) {
		dev_err(dev, "PM mbox_send_message failed: %d\n", ret);
		return ret;
	}

	ret = wait_for_completion_timeout(&oproc->pm_comp, to);
	if (!oproc->suspend_acked)
		return -EBUSY;

	/*
	 * The remoteproc side is returning the ACK message before saving the
	 * context, because the context saving is performed within a SYS/BIOS
	 * function, and it cannot have any inter-dependencies against the IPC
	 * layer. Also, as the SYS/BIOS needs to preserve properly the processor
	 * register set, sending this ACK or signalling the completion of the
	 * context save through a shared memory variable can never be the
	 * absolute last thing to be executed on the remoteproc side, and the
	 * MPU cannot use the ACK message as a sync point to put the remoteproc
	 * into reset. The only way to ensure that the remote processor has
	 * completed saving the context is to check that the module has reached
	 * STANDBY state (after saving the context, the SYS/BIOS executes the
	 * appropriate target-specific WFI instruction causing the module to
	 * enter STANDBY).
	 */
	while (!_is_rproc_in_standby(oproc)) {
		if (time_after(jiffies, ta))
			return -ETIME;
		schedule();
	}

	ret = reset_control_assert(oproc->reset);
	if (ret) {
		dev_err(dev, "reset assert during suspend failed %d\n", ret);
		return ret;
	}

	ret = omap_rproc_disable_timers(rproc, false);
	if (ret) {
		dev_err(dev, "disabling timers during suspend failed %d\n",
			ret);
		goto enable_device;
	}

	/*
	 * IOMMUs would have to be disabled specifically for runtime suspend.
	 * They are handled automatically through System PM callbacks for
	 * regular system suspend
	 */
	if (auto_suspend) {
		ret = omap_iommu_domain_deactivate(rproc->domain);
		if (ret) {
			dev_err(dev, "iommu domain deactivate failed %d\n",
				ret);
			goto enable_timers;
		}
	}

	return 0;

enable_timers:
	/* ignore errors on re-enabling code */
	omap_rproc_enable_timers(rproc, false);
enable_device:
	reset_control_deassert(oproc->reset);
	return ret;
}

static int _omap_rproc_resume(struct rproc *rproc, bool auto_suspend)
{
	struct device *dev = rproc->dev.parent;
	struct omap_rproc *oproc = rproc->priv;
	int ret;

	/*
	 * IOMMUs would have to be enabled specifically for runtime resume.
	 * They would have been already enabled automatically through System
	 * PM callbacks for regular system resume
	 */
	if (auto_suspend) {
		ret = omap_iommu_domain_activate(rproc->domain);
		if (ret) {
			dev_err(dev, "omap_iommu activate failed %d\n", ret);
			goto out;
		}
	}

	/* boot address could be lost after suspend, so restore it */
	if (oproc->boot_data) {
		ret = omap_rproc_write_dsp_boot_addr(rproc);
		if (ret) {
			dev_err(dev, "boot address restore failed %d\n", ret);
			goto suspend_iommu;
		}
	}

	ret = omap_rproc_enable_timers(rproc, false);
	if (ret) {
		dev_err(dev, "enabling timers during resume failed %d\n", ret);
		goto suspend_iommu;
	}

	ret = reset_control_deassert(oproc->reset);
	if (ret) {
		dev_err(dev, "reset deassert during resume failed %d\n", ret);
		goto disable_timers;
	}

	return 0;

disable_timers:
	omap_rproc_disable_timers(rproc, false);
suspend_iommu:
	if (auto_suspend)
		omap_iommu_domain_deactivate(rproc->domain);
out:
	return ret;
}

static int __maybe_unused omap_rproc_suspend(struct device *dev)
{
	struct rproc *rproc = dev_get_drvdata(dev);
	struct omap_rproc *oproc = rproc->priv;
	int ret = 0;

	mutex_lock(&rproc->lock);
	if (rproc->state == RPROC_OFFLINE)
		goto out;

	if (rproc->state == RPROC_SUSPENDED)
		goto out;

	if (rproc->state != RPROC_RUNNING) {
		ret = -EBUSY;
		goto out;
	}

	ret = _omap_rproc_suspend(rproc, false);
	if (ret) {
		dev_err(dev, "suspend failed %d\n", ret);
		goto out;
	}

	/*
	 * remoteproc is running at the time of system suspend, so remember
	 * it so as to wake it up during system resume
	 */
	oproc->need_resume = true;
	rproc->state = RPROC_SUSPENDED;

out:
	mutex_unlock(&rproc->lock);
	return ret;
}

static int __maybe_unused omap_rproc_resume(struct device *dev)
{
	struct rproc *rproc = dev_get_drvdata(dev);
	struct omap_rproc *oproc = rproc->priv;
	int ret = 0;

	mutex_lock(&rproc->lock);
	if (rproc->state == RPROC_OFFLINE)
		goto out;

	if (rproc->state != RPROC_SUSPENDED) {
		ret = -EBUSY;
		goto out;
	}

	/*
	 * remoteproc was auto-suspended at the time of system suspend,
	 * so no need to wake-up the processor (leave it in suspended
	 * state, will be woken up during a subsequent runtime_resume)
	 */
	if (!oproc->need_resume)
		goto out;

	ret = _omap_rproc_resume(rproc, false);
	if (ret) {
		dev_err(dev, "resume failed %d\n", ret);
		goto out;
	}

	oproc->need_resume = false;
	rproc->state = RPROC_RUNNING;

	pm_runtime_mark_last_busy(dev);
out:
	mutex_unlock(&rproc->lock);
	return ret;
}

static int omap_rproc_runtime_suspend(struct device *dev)
{
	struct rproc *rproc = dev_get_drvdata(dev);
	struct omap_rproc *oproc = rproc->priv;
	int ret;

	mutex_lock(&rproc->lock);
	if (rproc->state == RPROC_CRASHED) {
		dev_dbg(dev, "rproc cannot be runtime suspended when crashed!\n");
		ret = -EBUSY;
		goto out;
	}

	if (WARN_ON(rproc->state != RPROC_RUNNING)) {
		dev_err(dev, "rproc cannot be runtime suspended when not running!\n");
		ret = -EBUSY;
		goto out;
	}

	/*
	 * do not even attempt suspend if the remote processor is not
	 * idled for runtime auto-suspend
	 */
	if (!_is_rproc_in_standby(oproc)) {
		ret = -EBUSY;
		goto abort;
	}

	ret = _omap_rproc_suspend(rproc, true);
	if (ret)
		goto abort;

	rproc->state = RPROC_SUSPENDED;
	mutex_unlock(&rproc->lock);
	return 0;

abort:
	pm_runtime_mark_last_busy(dev);
out:
	mutex_unlock(&rproc->lock);
	return ret;
}

static int omap_rproc_runtime_resume(struct device *dev)
{
	struct rproc *rproc = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&rproc->lock);
	if (WARN_ON(rproc->state != RPROC_SUSPENDED)) {
		dev_err(dev, "rproc cannot be runtime resumed if not suspended! state=%d\n",
			rproc->state);
		ret = -EBUSY;
		goto out;
	}

	ret = _omap_rproc_resume(rproc, true);
	if (ret) {
		dev_err(dev, "runtime resume failed %d\n", ret);
		goto out;
	}

	rproc->state = RPROC_RUNNING;
out:
	mutex_unlock(&rproc->lock);
	return ret;
}
#endif /* CONFIG_PM */

static const struct omap_rproc_mem_data ipu_mems[] = {
	{ .name = "l2ram", .dev_addr = 0x20000000 },
	{ },
};

static const struct omap_rproc_mem_data dra7_dsp_mems[] = {
	{ .name = "l2ram", .dev_addr = 0x800000 },
	{ .name = "l1pram", .dev_addr = 0xe00000 },
	{ .name = "l1dram", .dev_addr = 0xf00000 },
	{ },
};

static const struct omap_rproc_dev_data omap4_dsp_dev_data = {
	.device_name	= "dsp",
};

static const struct omap_rproc_dev_data omap4_ipu_dev_data = {
	.device_name	= "ipu",
	.mems		= ipu_mems,
};

static const struct omap_rproc_dev_data omap5_dsp_dev_data = {
	.device_name	= "dsp",
};

static const struct omap_rproc_dev_data omap5_ipu_dev_data = {
	.device_name	= "ipu",
	.mems		= ipu_mems,
};

static const struct omap_rproc_dev_data dra7_dsp_dev_data = {
	.device_name	= "dsp",
	.mems		= dra7_dsp_mems,
};

static const struct omap_rproc_dev_data dra7_ipu_dev_data = {
	.device_name	= "ipu",
	.mems		= ipu_mems,
};

static const struct of_device_id omap_rproc_of_match[] = {
	{
		.compatible     = "ti,omap4-dsp",
		.data           = &omap4_dsp_dev_data,
	},
	{
		.compatible     = "ti,omap4-ipu",
		.data           = &omap4_ipu_dev_data,
	},
	{
		.compatible     = "ti,omap5-dsp",
		.data           = &omap5_dsp_dev_data,
	},
	{
		.compatible     = "ti,omap5-ipu",
		.data           = &omap5_ipu_dev_data,
	},
	{
		.compatible     = "ti,dra7-dsp",
		.data           = &dra7_dsp_dev_data,
	},
	{
		.compatible     = "ti,dra7-ipu",
		.data           = &dra7_ipu_dev_data,
	},
	{
		/* end */
	},
};
MODULE_DEVICE_TABLE(of, omap_rproc_of_match);

static const char *omap_rproc_get_firmware(struct platform_device *pdev)
{
	const char *fw_name;
	int ret;

	ret = of_property_read_string(pdev->dev.of_node, "firmware-name",
				      &fw_name);
	if (ret)
		return ERR_PTR(ret);

	return fw_name;
}

static int omap_rproc_get_boot_data(struct platform_device *pdev,
				    struct rproc *rproc)
{
	struct device_node *np = pdev->dev.of_node;
	struct omap_rproc *oproc = rproc->priv;
	const struct omap_rproc_dev_data *data;

	data = of_device_get_match_data(&pdev->dev);
	if (!data)
		return -ENODEV;

	if (!of_property_read_bool(np, "ti,bootreg"))
		return 0;

	oproc->boot_data = devm_kzalloc(&pdev->dev, sizeof(*oproc->boot_data),
					GFP_KERNEL);
	if (!oproc->boot_data)
		return -ENOMEM;

	oproc->boot_data->syscon =
			syscon_regmap_lookup_by_phandle(np, "ti,bootreg");
	if (IS_ERR(oproc->boot_data->syscon))
		return PTR_ERR(oproc->boot_data->syscon);

	if (of_property_read_u32_index(np, "ti,bootreg", 1,
				       &oproc->boot_data->boot_reg)) {
		dev_err(&pdev->dev, "couldn't get the boot register\n");
		return -EINVAL;
	}

	of_property_read_u32_index(np, "ti,bootreg", 2,
				   &oproc->boot_data->boot_reg_shift);

	return 0;
}

static int omap_rproc_of_get_internal_memories(struct platform_device *pdev,
					       struct rproc *rproc)
{
	struct omap_rproc *oproc = rproc->priv;
	struct device *dev = &pdev->dev;
	const struct omap_rproc_dev_data *data;
	struct resource *res;
	int num_mems;
	int i;

	data = of_device_get_match_data(dev);
	if (!data)
		return -ENODEV;

	if (!data->mems)
		return 0;

	num_mems = of_property_count_elems_of_size(dev->of_node, "reg",
						   sizeof(u32)) / 2;

	oproc->mem = devm_kcalloc(dev, num_mems, sizeof(*oproc->mem),
				  GFP_KERNEL);
	if (!oproc->mem)
		return -ENOMEM;

	for (i = 0; data->mems[i].name; i++) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   data->mems[i].name);
		if (!res) {
			dev_err(dev, "no memory defined for %s\n",
				data->mems[i].name);
			return -ENOMEM;
		}
		oproc->mem[i].cpu_addr = devm_ioremap_resource(dev, res);
		if (IS_ERR(oproc->mem[i].cpu_addr)) {
			dev_err(dev, "failed to parse and map %s memory\n",
				data->mems[i].name);
			return PTR_ERR(oproc->mem[i].cpu_addr);
		}
		oproc->mem[i].bus_addr = res->start;
		oproc->mem[i].dev_addr = data->mems[i].dev_addr;
		oproc->mem[i].size = resource_size(res);

		dev_dbg(dev, "memory %8s: bus addr %pa size 0x%x va %pK da 0x%x\n",
			data->mems[i].name, &oproc->mem[i].bus_addr,
			oproc->mem[i].size, oproc->mem[i].cpu_addr,
			oproc->mem[i].dev_addr);
	}
	oproc->num_mems = num_mems;

	return 0;
}

#ifdef CONFIG_OMAP_REMOTEPROC_WATCHDOG
static int omap_rproc_count_wdog_timers(struct device *dev)
{
	struct device_node *np = dev->of_node;
	int ret;

	ret = of_count_phandle_with_args(np, "ti,watchdog-timers", NULL);
	if (ret <= 0) {
		dev_dbg(dev, "device does not have watchdog timers, status = %d\n",
			ret);
		ret = 0;
	}

	return ret;
}
#else
static int omap_rproc_count_wdog_timers(struct device *dev)
{
	return 0;
}
#endif

static int omap_rproc_of_get_timers(struct platform_device *pdev,
				    struct rproc *rproc)
{
	struct device_node *np = pdev->dev.of_node;
	struct omap_rproc *oproc = rproc->priv;
	struct device *dev = &pdev->dev;
	int num_timers;

	/*
	 * Timer nodes are directly used in client nodes as phandles, so
	 * retrieve the count using appropriate size
	 */
	oproc->num_timers = of_count_phandle_with_args(np, "ti,timers", NULL);
	if (oproc->num_timers <= 0) {
		dev_dbg(dev, "device does not have timers, status = %d\n",
			oproc->num_timers);
		oproc->num_timers = 0;
	}

	oproc->num_wd_timers = omap_rproc_count_wdog_timers(dev);

	num_timers = oproc->num_timers + oproc->num_wd_timers;
	if (num_timers) {
		oproc->timers = devm_kcalloc(dev, num_timers,
					     sizeof(*oproc->timers),
					     GFP_KERNEL);
		if (!oproc->timers)
			return -ENOMEM;

		dev_dbg(dev, "device has %d tick timers and %d watchdog timers\n",
			oproc->num_timers, oproc->num_wd_timers);
	}

	return 0;
}

static void omap_rproc_mem_release(void *data)
{
	struct device *dev = data;

	of_reserved_mem_device_release(dev);
}

static int omap_rproc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct omap_rproc *oproc;
	struct rproc *rproc;
	const char *firmware;
	int ret;
	struct reset_control *reset;

	if (!np) {
		dev_err(&pdev->dev, "only DT-based devices are supported\n");
		return -ENODEV;
	}

	reset = devm_reset_control_array_get_exclusive(&pdev->dev);
	if (IS_ERR(reset))
		return PTR_ERR(reset);

	firmware = omap_rproc_get_firmware(pdev);
	if (IS_ERR(firmware))
		return PTR_ERR(firmware);

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "dma_set_coherent_mask: %d\n", ret);
		return ret;
	}

	rproc = devm_rproc_alloc(&pdev->dev, dev_name(&pdev->dev), &omap_rproc_ops,
				 firmware, sizeof(*oproc));
	if (!rproc)
		return -ENOMEM;

	oproc = rproc->priv;
	oproc->rproc = rproc;
	oproc->reset = reset;
	/* All existing OMAP IPU and DSP processors have an MMU */
	rproc->has_iommu = true;

#ifdef CONFIG_ARM_DMA_USE_IOMMU
	/*
	 * Throw away the ARM DMA mapping that we'll never use, so it doesn't
	 * interfere with the core rproc->domain and we get the right DMA ops.
	 */
	if (pdev->dev.archdata.mapping) {
		struct dma_iommu_mapping *mapping = to_dma_iommu_mapping(&pdev->dev);

		arm_iommu_detach_device(&pdev->dev);
		arm_iommu_release_mapping(mapping);
	}
#endif

	ret = omap_rproc_of_get_internal_memories(pdev, rproc);
	if (ret)
		return ret;

	ret = omap_rproc_get_boot_data(pdev, rproc);
	if (ret)
		return ret;

	ret = omap_rproc_of_get_timers(pdev, rproc);
	if (ret)
		return ret;

	init_completion(&oproc->pm_comp);
	oproc->autosuspend_delay = DEFAULT_AUTOSUSPEND_DELAY;

	of_property_read_u32(pdev->dev.of_node, "ti,autosuspend-delay-ms",
			     &oproc->autosuspend_delay);

	pm_runtime_set_autosuspend_delay(&pdev->dev, oproc->autosuspend_delay);

	oproc->fck = devm_clk_get(&pdev->dev, 0);
	if (IS_ERR(oproc->fck))
		return PTR_ERR(oproc->fck);

	ret = of_reserved_mem_device_init(&pdev->dev);
	if (ret) {
		dev_warn(&pdev->dev, "device does not have specific CMA pool.\n");
		dev_warn(&pdev->dev, "Typically this should be provided,\n");
		dev_warn(&pdev->dev, "only omit if you know what you are doing.\n");
	}
	ret = devm_add_action_or_reset(&pdev->dev, omap_rproc_mem_release, &pdev->dev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, rproc);

	ret = devm_rproc_add(&pdev->dev, rproc);
	if (ret)
		return ret;

	return 0;
}

static const struct dev_pm_ops omap_rproc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(omap_rproc_suspend, omap_rproc_resume)
	SET_RUNTIME_PM_OPS(omap_rproc_runtime_suspend,
			   omap_rproc_runtime_resume, NULL)
};

static struct platform_driver omap_rproc_driver = {
	.probe = omap_rproc_probe,
	.driver = {
		.name = "omap-rproc",
		.pm = &omap_rproc_pm_ops,
		.of_match_table = omap_rproc_of_match,
	},
};

module_platform_driver(omap_rproc_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("OMAP Remote Processor control driver");

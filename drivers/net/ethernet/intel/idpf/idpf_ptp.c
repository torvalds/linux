// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2024 Intel Corporation */

#include "idpf.h"
#include "idpf_ptp.h"

/**
 * idpf_ptp_get_access - Determine the access type of the PTP features
 * @adapter: Driver specific private structure
 * @direct: Capability that indicates the direct access
 * @mailbox: Capability that indicates the mailbox access
 *
 * Return: the type of supported access for the PTP feature.
 */
static enum idpf_ptp_access
idpf_ptp_get_access(const struct idpf_adapter *adapter, u32 direct, u32 mailbox)
{
	if (adapter->ptp->caps & direct)
		return IDPF_PTP_DIRECT;
	else if (adapter->ptp->caps & mailbox)
		return IDPF_PTP_MAILBOX;
	else
		return IDPF_PTP_NONE;
}

/**
 * idpf_ptp_get_features_access - Determine the access type of PTP features
 * @adapter: Driver specific private structure
 *
 * Fulfill the adapter structure with type of the supported PTP features
 * access.
 */
void idpf_ptp_get_features_access(const struct idpf_adapter *adapter)
{
	struct idpf_ptp *ptp = adapter->ptp;
	u32 direct, mailbox;

	/* Get the device clock time */
	direct = VIRTCHNL2_CAP_PTP_GET_DEVICE_CLK_TIME;
	mailbox = VIRTCHNL2_CAP_PTP_GET_DEVICE_CLK_TIME_MB;
	ptp->get_dev_clk_time_access = idpf_ptp_get_access(adapter,
							   direct,
							   mailbox);

	/* Get the cross timestamp */
	direct = VIRTCHNL2_CAP_PTP_GET_CROSS_TIME;
	mailbox = VIRTCHNL2_CAP_PTP_GET_CROSS_TIME_MB;
	ptp->get_cross_tstamp_access = idpf_ptp_get_access(adapter,
							   direct,
							   mailbox);

	/* Set the device clock time */
	direct = VIRTCHNL2_CAP_PTP_SET_DEVICE_CLK_TIME;
	mailbox = VIRTCHNL2_CAP_PTP_SET_DEVICE_CLK_TIME;
	ptp->set_dev_clk_time_access = idpf_ptp_get_access(adapter,
							   direct,
							   mailbox);

	/* Adjust the device clock time */
	direct = VIRTCHNL2_CAP_PTP_ADJ_DEVICE_CLK;
	mailbox = VIRTCHNL2_CAP_PTP_ADJ_DEVICE_CLK_MB;
	ptp->adj_dev_clk_time_access = idpf_ptp_get_access(adapter,
							   direct,
							   mailbox);

	/* Tx timestamping */
	direct = VIRTCHNL2_CAP_PTP_TX_TSTAMPS;
	mailbox = VIRTCHNL2_CAP_PTP_TX_TSTAMPS_MB;
	ptp->tx_tstamp_access = idpf_ptp_get_access(adapter,
						    direct,
						    mailbox);
}

/**
 * idpf_ptp_enable_shtime - Enable shadow time and execute a command
 * @adapter: Driver specific private structure
 */
static void idpf_ptp_enable_shtime(struct idpf_adapter *adapter)
{
	u32 shtime_enable, exec_cmd;

	/* Get offsets */
	shtime_enable = adapter->ptp->cmd.shtime_enable_mask;
	exec_cmd = adapter->ptp->cmd.exec_cmd_mask;

	/* Set the shtime en and the sync field */
	writel(shtime_enable, adapter->ptp->dev_clk_regs.cmd_sync);
	writel(exec_cmd | shtime_enable, adapter->ptp->dev_clk_regs.cmd_sync);
}

/**
 * idpf_ptp_read_src_clk_reg_direct - Read directly the main timer value
 * @adapter: Driver specific private structure
 * @sts: Optional parameter for holding a pair of system timestamps from
 *	 the system clock. Will be ignored when NULL is given.
 *
 * Return: the device clock time.
 */
static u64 idpf_ptp_read_src_clk_reg_direct(struct idpf_adapter *adapter,
					    struct ptp_system_timestamp *sts)
{
	struct idpf_ptp *ptp = adapter->ptp;
	u32 hi, lo;

	spin_lock(&ptp->read_dev_clk_lock);

	/* Read the system timestamp pre PHC read */
	ptp_read_system_prets(sts);

	idpf_ptp_enable_shtime(adapter);

	/* Read the system timestamp post PHC read */
	ptp_read_system_postts(sts);

	lo = readl(ptp->dev_clk_regs.dev_clk_ns_l);
	hi = readl(ptp->dev_clk_regs.dev_clk_ns_h);

	spin_unlock(&ptp->read_dev_clk_lock);

	return ((u64)hi << 32) | lo;
}

/**
 * idpf_ptp_read_src_clk_reg_mailbox - Read the main timer value through mailbox
 * @adapter: Driver specific private structure
 * @sts: Optional parameter for holding a pair of system timestamps from
 *	 the system clock. Will be ignored when NULL is given.
 * @src_clk: Returned main timer value in nanoseconds unit
 *
 * Return: 0 on success, -errno otherwise.
 */
static int idpf_ptp_read_src_clk_reg_mailbox(struct idpf_adapter *adapter,
					     struct ptp_system_timestamp *sts,
					     u64 *src_clk)
{
	struct idpf_ptp_dev_timers clk_time;
	int err;

	/* Read the system timestamp pre PHC read */
	ptp_read_system_prets(sts);

	err = idpf_ptp_get_dev_clk_time(adapter, &clk_time);
	if (err)
		return err;

	/* Read the system timestamp post PHC read */
	ptp_read_system_postts(sts);

	*src_clk = clk_time.dev_clk_time_ns;

	return 0;
}

/**
 * idpf_ptp_read_src_clk_reg - Read the main timer value
 * @adapter: Driver specific private structure
 * @src_clk: Returned main timer value in nanoseconds unit
 * @sts: Optional parameter for holding a pair of system timestamps from
 *	 the system clock. Will be ignored if NULL is given.
 *
 * Return: the device clock time on success, -errno otherwise.
 */
static int idpf_ptp_read_src_clk_reg(struct idpf_adapter *adapter, u64 *src_clk,
				     struct ptp_system_timestamp *sts)
{
	switch (adapter->ptp->get_dev_clk_time_access) {
	case IDPF_PTP_NONE:
		return -EOPNOTSUPP;
	case IDPF_PTP_MAILBOX:
		return idpf_ptp_read_src_clk_reg_mailbox(adapter, sts, src_clk);
	case IDPF_PTP_DIRECT:
		*src_clk = idpf_ptp_read_src_clk_reg_direct(adapter, sts);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

#if IS_ENABLED(CONFIG_ARM_ARCH_TIMER) || IS_ENABLED(CONFIG_X86)
/**
 * idpf_ptp_get_sync_device_time_direct - Get the cross time stamp values
 *					  directly
 * @adapter: Driver specific private structure
 * @dev_time: 64bit main timer value
 * @sys_time: 64bit system time value
 */
static void idpf_ptp_get_sync_device_time_direct(struct idpf_adapter *adapter,
						 u64 *dev_time, u64 *sys_time)
{
	u32 dev_time_lo, dev_time_hi, sys_time_lo, sys_time_hi;
	struct idpf_ptp *ptp = adapter->ptp;

	spin_lock(&ptp->read_dev_clk_lock);

	idpf_ptp_enable_shtime(adapter);

	dev_time_lo = readl(ptp->dev_clk_regs.dev_clk_ns_l);
	dev_time_hi = readl(ptp->dev_clk_regs.dev_clk_ns_h);

	sys_time_lo = readl(ptp->dev_clk_regs.sys_time_ns_l);
	sys_time_hi = readl(ptp->dev_clk_regs.sys_time_ns_h);

	spin_unlock(&ptp->read_dev_clk_lock);

	*dev_time = (u64)dev_time_hi << 32 | dev_time_lo;
	*sys_time = (u64)sys_time_hi << 32 | sys_time_lo;
}

/**
 * idpf_ptp_get_sync_device_time_mailbox - Get the cross time stamp values
 *					   through mailbox
 * @adapter: Driver specific private structure
 * @dev_time: 64bit main timer value expressed in nanoseconds
 * @sys_time: 64bit system time value expressed in nanoseconds
 *
 * Return: 0 on success, -errno otherwise.
 */
static int idpf_ptp_get_sync_device_time_mailbox(struct idpf_adapter *adapter,
						 u64 *dev_time, u64 *sys_time)
{
	struct idpf_ptp_dev_timers cross_time;
	int err;

	err = idpf_ptp_get_cross_time(adapter, &cross_time);
	if (err)
		return err;

	*dev_time = cross_time.dev_clk_time_ns;
	*sys_time = cross_time.sys_time_ns;

	return err;
}

/**
 * idpf_ptp_get_sync_device_time - Get the cross time stamp info
 * @device: Current device time
 * @system: System counter value read synchronously with device time
 * @ctx: Context provided by timekeeping code
 *
 * The device and the system clocks time read simultaneously.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int idpf_ptp_get_sync_device_time(ktime_t *device,
					 struct system_counterval_t *system,
					 void *ctx)
{
	struct idpf_adapter *adapter = ctx;
	u64 ns_time_dev, ns_time_sys;
	int err;

	switch (adapter->ptp->get_cross_tstamp_access) {
	case IDPF_PTP_NONE:
		return -EOPNOTSUPP;
	case IDPF_PTP_DIRECT:
		idpf_ptp_get_sync_device_time_direct(adapter, &ns_time_dev,
						     &ns_time_sys);
		break;
	case IDPF_PTP_MAILBOX:
		err = idpf_ptp_get_sync_device_time_mailbox(adapter,
							    &ns_time_dev,
							    &ns_time_sys);
		if (err)
			return err;
		break;
	default:
		return -EOPNOTSUPP;
	}

	*device = ns_to_ktime(ns_time_dev);

	system->cs_id = IS_ENABLED(CONFIG_X86) ? CSID_X86_ART
					       : CSID_ARM_ARCH_COUNTER;
	system->cycles = ns_time_sys;
	system->use_nsecs = true;

	return 0;
}

/**
 * idpf_ptp_get_crosststamp - Capture a device cross timestamp
 * @info: the driver's PTP info structure
 * @cts: The memory to fill the cross timestamp info
 *
 * Capture a cross timestamp between the system time and the device PTP hardware
 * clock.
 *
 * Return: cross timestamp value on success, -errno on failure.
 */
static int idpf_ptp_get_crosststamp(struct ptp_clock_info *info,
				    struct system_device_crosststamp *cts)
{
	struct idpf_adapter *adapter = idpf_ptp_info_to_adapter(info);

	return get_device_system_crosststamp(idpf_ptp_get_sync_device_time,
					     adapter, NULL, cts);
}
#endif /* CONFIG_ARM_ARCH_TIMER || CONFIG_X86 */

/**
 * idpf_ptp_gettimex64 - Get the time of the clock
 * @info: the driver's PTP info structure
 * @ts: timespec64 structure to hold the current time value
 * @sts: Optional parameter for holding a pair of system timestamps from
 *	 the system clock. Will be ignored if NULL is given.
 *
 * Return: the device clock value in ns, after converting it into a timespec
 * struct on success, -errno otherwise.
 */
static int idpf_ptp_gettimex64(struct ptp_clock_info *info,
			       struct timespec64 *ts,
			       struct ptp_system_timestamp *sts)
{
	struct idpf_adapter *adapter = idpf_ptp_info_to_adapter(info);
	u64 time_ns;
	int err;

	err = idpf_ptp_read_src_clk_reg(adapter, &time_ns, sts);
	if (err)
		return -EACCES;

	*ts = ns_to_timespec64(time_ns);

	return 0;
}

/**
 * idpf_ptp_update_phctime_rxq_grp - Update the cached PHC time for a given Rx
 *				     queue group.
 * @grp: receive queue group in which Rx timestamp is enabled
 * @split: Indicates whether the queue model is split or single queue
 * @systime: Cached system time
 */
static void
idpf_ptp_update_phctime_rxq_grp(const struct idpf_rxq_group *grp, bool split,
				u64 systime)
{
	struct idpf_rx_queue *rxq;
	u16 i;

	if (!split) {
		for (i = 0; i < grp->singleq.num_rxq; i++) {
			rxq = grp->singleq.rxqs[i];
			if (rxq)
				WRITE_ONCE(rxq->cached_phc_time, systime);
		}
	} else {
		for (i = 0; i < grp->splitq.num_rxq_sets; i++) {
			rxq = &grp->splitq.rxq_sets[i]->rxq;
			if (rxq)
				WRITE_ONCE(rxq->cached_phc_time, systime);
		}
	}
}

/**
 * idpf_ptp_update_cached_phctime - Update the cached PHC time values
 * @adapter: Driver specific private structure
 *
 * This function updates the system time values which are cached in the adapter
 * structure and the Rx queues.
 *
 * This function must be called periodically to ensure that the cached value
 * is never more than 2 seconds old.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int idpf_ptp_update_cached_phctime(struct idpf_adapter *adapter)
{
	u64 systime;
	int err;

	err = idpf_ptp_read_src_clk_reg(adapter, &systime, NULL);
	if (err)
		return -EACCES;

	/* Update the cached PHC time stored in the adapter structure.
	 * These values are used to extend Tx timestamp values to 64 bit
	 * expected by the stack.
	 */
	WRITE_ONCE(adapter->ptp->cached_phc_time, systime);
	WRITE_ONCE(adapter->ptp->cached_phc_jiffies, jiffies);

	idpf_for_each_vport(adapter, vport) {
		bool split;

		if (!vport || !vport->rxq_grps)
			continue;

		split = idpf_is_queue_model_split(vport->rxq_model);

		for (u16 i = 0; i < vport->num_rxq_grp; i++) {
			struct idpf_rxq_group *grp = &vport->rxq_grps[i];

			idpf_ptp_update_phctime_rxq_grp(grp, split, systime);
		}
	}

	return 0;
}

/**
 * idpf_ptp_settime64 - Set the time of the clock
 * @info: the driver's PTP info structure
 * @ts: timespec64 structure that holds the new time value
 *
 * Set the device clock to the user input value. The conversion from timespec
 * to ns happens in the write function.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int idpf_ptp_settime64(struct ptp_clock_info *info,
			      const struct timespec64 *ts)
{
	struct idpf_adapter *adapter = idpf_ptp_info_to_adapter(info);
	enum idpf_ptp_access access;
	int err;
	u64 ns;

	access = adapter->ptp->set_dev_clk_time_access;
	if (access != IDPF_PTP_MAILBOX)
		return -EOPNOTSUPP;

	ns = timespec64_to_ns(ts);

	err = idpf_ptp_set_dev_clk_time(adapter, ns);
	if (err) {
		pci_err(adapter->pdev, "Failed to set the time, err: %pe\n",
			ERR_PTR(err));
		return err;
	}

	err = idpf_ptp_update_cached_phctime(adapter);
	if (err)
		pci_warn(adapter->pdev,
			 "Unable to immediately update cached PHC time\n");

	return 0;
}

/**
 * idpf_ptp_adjtime_nonatomic - Do a non-atomic clock adjustment
 * @info: the driver's PTP info structure
 * @delta: Offset in nanoseconds to adjust the time by
 *
 * Return: 0 on success, -errno otherwise.
 */
static int idpf_ptp_adjtime_nonatomic(struct ptp_clock_info *info, s64 delta)
{
	struct timespec64 now, then;
	int err;

	err = idpf_ptp_gettimex64(info, &now, NULL);
	if (err)
		return err;

	then = ns_to_timespec64(delta);
	now = timespec64_add(now, then);

	return idpf_ptp_settime64(info, &now);
}

/**
 * idpf_ptp_adjtime - Adjust the time of the clock by the indicated delta
 * @info: the driver's PTP info structure
 * @delta: Offset in nanoseconds to adjust the time by
 *
 * Return: 0 on success, -errno otherwise.
 */
static int idpf_ptp_adjtime(struct ptp_clock_info *info, s64 delta)
{
	struct idpf_adapter *adapter = idpf_ptp_info_to_adapter(info);
	enum idpf_ptp_access access;
	int err;

	access = adapter->ptp->adj_dev_clk_time_access;
	if (access != IDPF_PTP_MAILBOX)
		return -EOPNOTSUPP;

	/* Hardware only supports atomic adjustments using signed 32-bit
	 * integers. For any adjustment outside this range, perform
	 * a non-atomic get->adjust->set flow.
	 */
	if (delta > S32_MAX || delta < S32_MIN)
		return idpf_ptp_adjtime_nonatomic(info, delta);

	err = idpf_ptp_adj_dev_clk_time(adapter, delta);
	if (err) {
		pci_err(adapter->pdev, "Failed to adjust the clock with delta %lld err: %pe\n",
			delta, ERR_PTR(err));
		return err;
	}

	err = idpf_ptp_update_cached_phctime(adapter);
	if (err)
		pci_warn(adapter->pdev,
			 "Unable to immediately update cached PHC time\n");

	return 0;
}

/**
 * idpf_ptp_adjfine - Adjust clock increment rate
 * @info: the driver's PTP info structure
 * @scaled_ppm: Parts per million with 16-bit fractional field
 *
 * Adjust the frequency of the clock by the indicated scaled ppm from the
 * base frequency.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int idpf_ptp_adjfine(struct ptp_clock_info *info, long scaled_ppm)
{
	struct idpf_adapter *adapter = idpf_ptp_info_to_adapter(info);
	enum idpf_ptp_access access;
	u64 incval, diff;
	int err;

	access = adapter->ptp->adj_dev_clk_time_access;
	if (access != IDPF_PTP_MAILBOX)
		return -EOPNOTSUPP;

	incval = adapter->ptp->base_incval;

	diff = adjust_by_scaled_ppm(incval, scaled_ppm);
	err = idpf_ptp_adj_dev_clk_fine(adapter, diff);
	if (err)
		pci_err(adapter->pdev, "Failed to adjust clock increment rate for scaled ppm %ld %pe\n",
			scaled_ppm, ERR_PTR(err));

	return 0;
}

/**
 * idpf_ptp_verify_pin - Verify if pin supports requested pin function
 * @info: the driver's PTP info structure
 * @pin: Pin index
 * @func: Assigned function
 * @chan: Assigned channel
 *
 * Return: EOPNOTSUPP as not supported yet.
 */
static int idpf_ptp_verify_pin(struct ptp_clock_info *info, unsigned int pin,
			       enum ptp_pin_function func, unsigned int chan)
{
	return -EOPNOTSUPP;
}

/**
 * idpf_ptp_gpio_enable - Enable/disable ancillary features of PHC
 * @info: the driver's PTP info structure
 * @rq: The requested feature to change
 * @on: Enable/disable flag
 *
 * Return: EOPNOTSUPP as not supported yet.
 */
static int idpf_ptp_gpio_enable(struct ptp_clock_info *info,
				struct ptp_clock_request *rq, int on)
{
	return -EOPNOTSUPP;
}

/**
 * idpf_ptp_tstamp_extend_32b_to_64b - Convert a 32b nanoseconds Tx or Rx
 *				       timestamp value to 64b.
 * @cached_phc_time: recently cached copy of PHC time
 * @in_timestamp: Ingress/egress 32b nanoseconds timestamp value
 *
 * Hardware captures timestamps which contain only 32 bits of nominal
 * nanoseconds, as opposed to the 64bit timestamps that the stack expects.
 *
 * Return: Tx timestamp value extended to 64 bits based on cached PHC time.
 */
u64 idpf_ptp_tstamp_extend_32b_to_64b(u64 cached_phc_time, u32 in_timestamp)
{
	u32 delta, phc_time_lo;
	u64 ns;

	/* Extract the lower 32 bits of the PHC time */
	phc_time_lo = (u32)cached_phc_time;

	/* Calculate the delta between the lower 32bits of the cached PHC
	 * time and the in_timestamp value.
	 */
	delta = in_timestamp - phc_time_lo;

	if (delta > U32_MAX / 2) {
		/* Reverse the delta calculation here */
		delta = phc_time_lo - in_timestamp;
		ns = cached_phc_time - delta;
	} else {
		ns = cached_phc_time + delta;
	}

	return ns;
}

/**
 * idpf_ptp_extend_ts - Convert a 40b timestamp to 64b nanoseconds
 * @vport: Virtual port structure
 * @in_tstamp: Ingress/egress timestamp value
 *
 * It is assumed that the caller verifies the timestamp is valid prior to
 * calling this function.
 *
 * Extract the 32bit nominal nanoseconds and extend them. Use the cached PHC
 * time stored in the device private PTP structure as the basis for timestamp
 * extension.
 *
 * Return: Tx timestamp value extended to 64 bits.
 */
u64 idpf_ptp_extend_ts(struct idpf_vport *vport, u64 in_tstamp)
{
	struct idpf_ptp *ptp = vport->adapter->ptp;
	unsigned long discard_time;

	discard_time = ptp->cached_phc_jiffies + 2 * HZ;

	if (time_is_before_jiffies(discard_time)) {
		u64_stats_update_begin(&vport->tstamp_stats.stats_sync);
		u64_stats_inc(&vport->tstamp_stats.discarded);
		u64_stats_update_end(&vport->tstamp_stats.stats_sync);

		return 0;
	}

	return idpf_ptp_tstamp_extend_32b_to_64b(ptp->cached_phc_time,
						 lower_32_bits(in_tstamp));
}

/**
 * idpf_ptp_request_ts - Request an available Tx timestamp index
 * @tx_q: Transmit queue on which the Tx timestamp is requested
 * @skb: The SKB to associate with this timestamp request
 * @idx: Index of the Tx timestamp latch
 *
 * Request tx timestamp index negotiated during PTP init that will be set into
 * Tx descriptor.
 *
 * Return: 0 and the index that can be provided to Tx descriptor on success,
 * -errno otherwise.
 */
int idpf_ptp_request_ts(struct idpf_tx_queue *tx_q, struct sk_buff *skb,
			u32 *idx)
{
	struct idpf_ptp_tx_tstamp *ptp_tx_tstamp;
	struct list_head *head;

	/* Get the index from the free latches list */
	spin_lock(&tx_q->cached_tstamp_caps->latches_lock);

	head = &tx_q->cached_tstamp_caps->latches_free;
	if (list_empty(head)) {
		spin_unlock(&tx_q->cached_tstamp_caps->latches_lock);
		return -ENOBUFS;
	}

	ptp_tx_tstamp = list_first_entry(head, struct idpf_ptp_tx_tstamp,
					 list_member);
	list_del(&ptp_tx_tstamp->list_member);

	ptp_tx_tstamp->skb = skb_get(skb);
	skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

	/* Move the element to the used latches list */
	list_add(&ptp_tx_tstamp->list_member,
		 &tx_q->cached_tstamp_caps->latches_in_use);
	spin_unlock(&tx_q->cached_tstamp_caps->latches_lock);

	*idx = ptp_tx_tstamp->idx;

	return 0;
}

/**
 * idpf_ptp_set_rx_tstamp - Enable or disable Rx timestamping
 * @vport: Virtual port structure
 * @rx_filter: Receive timestamp filter
 */
static void idpf_ptp_set_rx_tstamp(struct idpf_vport *vport, int rx_filter)
{
	bool enable = true, splitq;

	splitq = idpf_is_queue_model_split(vport->rxq_model);

	if (rx_filter == HWTSTAMP_FILTER_NONE) {
		enable = false;
		vport->tstamp_config.rx_filter = HWTSTAMP_FILTER_NONE;
	} else {
		vport->tstamp_config.rx_filter = HWTSTAMP_FILTER_ALL;
	}

	for (u16 i = 0; i < vport->num_rxq_grp; i++) {
		struct idpf_rxq_group *grp = &vport->rxq_grps[i];
		struct idpf_rx_queue *rx_queue;
		u16 j, num_rxq;

		if (splitq)
			num_rxq = grp->splitq.num_rxq_sets;
		else
			num_rxq = grp->singleq.num_rxq;

		for (j = 0; j < num_rxq; j++) {
			if (splitq)
				rx_queue = &grp->splitq.rxq_sets[j]->rxq;
			else
				rx_queue = grp->singleq.rxqs[j];

			if (enable)
				idpf_queue_set(PTP, rx_queue);
			else
				idpf_queue_clear(PTP, rx_queue);
		}
	}
}

/**
 * idpf_ptp_set_timestamp_mode - Setup driver for requested timestamp mode
 * @vport: Virtual port structure
 * @config: Hwtstamp settings requested or saved
 *
 * Return: 0 on success, -errno otherwise.
 */
int idpf_ptp_set_timestamp_mode(struct idpf_vport *vport,
				struct kernel_hwtstamp_config *config)
{
	switch (config->tx_type) {
	case HWTSTAMP_TX_OFF:
		break;
	case HWTSTAMP_TX_ON:
		if (!idpf_ptp_is_vport_tx_tstamp_ena(vport))
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	vport->tstamp_config.tx_type = config->tx_type;
	idpf_ptp_set_rx_tstamp(vport, config->rx_filter);
	*config = vport->tstamp_config;

	return 0;
}

/**
 * idpf_tstamp_task - Delayed task to handle Tx tstamps
 * @work: work_struct handle
 */
void idpf_tstamp_task(struct work_struct *work)
{
	struct idpf_vport *vport;

	vport = container_of(work, struct idpf_vport, tstamp_task);

	idpf_ptp_get_tx_tstamp(vport);
}

/**
 * idpf_ptp_do_aux_work - Do PTP periodic work
 * @info: Driver's PTP info structure
 *
 * Return: Number of jiffies to periodic work.
 */
static long idpf_ptp_do_aux_work(struct ptp_clock_info *info)
{
	struct idpf_adapter *adapter = idpf_ptp_info_to_adapter(info);

	idpf_ptp_update_cached_phctime(adapter);

	return msecs_to_jiffies(500);
}

/**
 * idpf_ptp_set_caps - Set PTP capabilities
 * @adapter: Driver specific private structure
 *
 * This function sets the PTP functions.
 */
static void idpf_ptp_set_caps(const struct idpf_adapter *adapter)
{
	struct ptp_clock_info *info = &adapter->ptp->info;

	snprintf(info->name, sizeof(info->name), "%s-%s-clk",
		 KBUILD_MODNAME, pci_name(adapter->pdev));

	info->owner = THIS_MODULE;
	info->max_adj = adapter->ptp->max_adj;
	info->gettimex64 = idpf_ptp_gettimex64;
	info->settime64 = idpf_ptp_settime64;
	info->adjfine = idpf_ptp_adjfine;
	info->adjtime = idpf_ptp_adjtime;
	info->verify = idpf_ptp_verify_pin;
	info->enable = idpf_ptp_gpio_enable;
	info->do_aux_work = idpf_ptp_do_aux_work;
#if IS_ENABLED(CONFIG_ARM_ARCH_TIMER)
	info->getcrosststamp = idpf_ptp_get_crosststamp;
#elif IS_ENABLED(CONFIG_X86)
	if (pcie_ptm_enabled(adapter->pdev) &&
	    boot_cpu_has(X86_FEATURE_ART) &&
	    boot_cpu_has(X86_FEATURE_TSC_KNOWN_FREQ))
		info->getcrosststamp = idpf_ptp_get_crosststamp;
#endif /* CONFIG_ARM_ARCH_TIMER */
}

/**
 * idpf_ptp_create_clock - Create PTP clock device for userspace
 * @adapter: Driver specific private structure
 *
 * This function creates a new PTP clock device.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int idpf_ptp_create_clock(const struct idpf_adapter *adapter)
{
	struct ptp_clock *clock;

	idpf_ptp_set_caps(adapter);

	/* Attempt to register the clock before enabling the hardware. */
	clock = ptp_clock_register(&adapter->ptp->info,
				   &adapter->pdev->dev);
	if (IS_ERR(clock)) {
		pci_err(adapter->pdev, "PTP clock creation failed: %pe\n",
			clock);
		return PTR_ERR(clock);
	}

	adapter->ptp->clock = clock;

	return 0;
}

/**
 * idpf_ptp_release_vport_tstamp - Release the Tx timestamps trakcers for a
 *				   given vport.
 * @vport: Virtual port structure
 *
 * Remove the queues and delete lists that tracks Tx timestamp entries for a
 * given vport.
 */
static void idpf_ptp_release_vport_tstamp(struct idpf_vport *vport)
{
	struct idpf_ptp_tx_tstamp *ptp_tx_tstamp, *tmp;
	struct list_head *head;

	cancel_work_sync(&vport->tstamp_task);

	/* Remove list with free latches */
	spin_lock_bh(&vport->tx_tstamp_caps->latches_lock);

	head = &vport->tx_tstamp_caps->latches_free;
	list_for_each_entry_safe(ptp_tx_tstamp, tmp, head, list_member) {
		list_del(&ptp_tx_tstamp->list_member);
		kfree(ptp_tx_tstamp);
	}

	/* Remove list with latches in use */
	head = &vport->tx_tstamp_caps->latches_in_use;
	u64_stats_update_begin(&vport->tstamp_stats.stats_sync);
	list_for_each_entry_safe(ptp_tx_tstamp, tmp, head, list_member) {
		u64_stats_inc(&vport->tstamp_stats.flushed);

		list_del(&ptp_tx_tstamp->list_member);
		if (ptp_tx_tstamp->skb)
			consume_skb(ptp_tx_tstamp->skb);

		kfree(ptp_tx_tstamp);
	}
	u64_stats_update_end(&vport->tstamp_stats.stats_sync);

	spin_unlock_bh(&vport->tx_tstamp_caps->latches_lock);

	kfree(vport->tx_tstamp_caps);
	vport->tx_tstamp_caps = NULL;
}

/**
 * idpf_ptp_release_tstamp - Release the Tx timestamps trackers
 * @adapter: Driver specific private structure
 *
 * Remove the queues and delete lists that tracks Tx timestamp entries.
 */
static void idpf_ptp_release_tstamp(struct idpf_adapter *adapter)
{
	idpf_for_each_vport(adapter, vport) {
		if (!idpf_ptp_is_vport_tx_tstamp_ena(vport))
			continue;

		idpf_ptp_release_vport_tstamp(vport);
	}
}

/**
 * idpf_ptp_get_txq_tstamp_capability - Verify the timestamping capability
 *					for a given tx queue.
 * @txq: Transmit queue
 *
 * Since performing timestamp flows requires reading the device clock value and
 * the support in the Control Plane, the function checks both factors and
 * summarizes the support for the timestamping.
 *
 * Return: true if the timestamping is supported, false otherwise.
 */
bool idpf_ptp_get_txq_tstamp_capability(struct idpf_tx_queue *txq)
{
	if (!txq || !txq->cached_tstamp_caps)
		return false;
	else if (txq->cached_tstamp_caps->access)
		return true;
	else
		return false;
}

/**
 * idpf_ptp_init - Initialize PTP hardware clock support
 * @adapter: Driver specific private structure
 *
 * Set up the device for interacting with the PTP hardware clock for all
 * functions. Function will allocate and register a ptp_clock with the
 * PTP_1588_CLOCK infrastructure.
 *
 * Return: 0 on success, -errno otherwise.
 */
int idpf_ptp_init(struct idpf_adapter *adapter)
{
	struct timespec64 ts;
	int err;

	if (!idpf_is_cap_ena(adapter, IDPF_OTHER_CAPS, VIRTCHNL2_CAP_PTP)) {
		pci_dbg(adapter->pdev, "PTP capability is not detected\n");
		return -EOPNOTSUPP;
	}

	adapter->ptp = kzalloc(sizeof(*adapter->ptp), GFP_KERNEL);
	if (!adapter->ptp)
		return -ENOMEM;

	/* add a back pointer to adapter */
	adapter->ptp->adapter = adapter;

	if (adapter->dev_ops.reg_ops.ptp_reg_init)
		adapter->dev_ops.reg_ops.ptp_reg_init(adapter);

	err = idpf_ptp_get_caps(adapter);
	if (err) {
		pci_err(adapter->pdev, "Failed to get PTP caps err %d\n", err);
		goto free_ptp;
	}

	err = idpf_ptp_create_clock(adapter);
	if (err)
		goto free_ptp;

	if (adapter->ptp->get_dev_clk_time_access != IDPF_PTP_NONE)
		ptp_schedule_worker(adapter->ptp->clock, 0);

	/* Write the default increment time value if the clock adjustments
	 * are enabled.
	 */
	if (adapter->ptp->adj_dev_clk_time_access != IDPF_PTP_NONE) {
		err = idpf_ptp_adj_dev_clk_fine(adapter,
						adapter->ptp->base_incval);
		if (err)
			goto remove_clock;
	}

	/* Write the initial time value if the set time operation is enabled */
	if (adapter->ptp->set_dev_clk_time_access != IDPF_PTP_NONE) {
		ts = ktime_to_timespec64(ktime_get_real());
		err = idpf_ptp_settime64(&adapter->ptp->info, &ts);
		if (err)
			goto remove_clock;
	}

	spin_lock_init(&adapter->ptp->read_dev_clk_lock);

	pci_dbg(adapter->pdev, "PTP init successful\n");

	return 0;

remove_clock:
	if (adapter->ptp->get_dev_clk_time_access != IDPF_PTP_NONE)
		ptp_cancel_worker_sync(adapter->ptp->clock);

	ptp_clock_unregister(adapter->ptp->clock);
	adapter->ptp->clock = NULL;

free_ptp:
	kfree(adapter->ptp);
	adapter->ptp = NULL;

	return err;
}

/**
 * idpf_ptp_release - Clear PTP hardware clock support
 * @adapter: Driver specific private structure
 */
void idpf_ptp_release(struct idpf_adapter *adapter)
{
	struct idpf_ptp *ptp = adapter->ptp;

	if (!ptp)
		return;

	if (ptp->tx_tstamp_access != IDPF_PTP_NONE &&
	    ptp->get_dev_clk_time_access != IDPF_PTP_NONE)
		idpf_ptp_release_tstamp(adapter);

	if (ptp->clock) {
		if (adapter->ptp->get_dev_clk_time_access != IDPF_PTP_NONE)
			ptp_cancel_worker_sync(adapter->ptp->clock);

		ptp_clock_unregister(ptp->clock);
	}

	kfree(ptp);
	adapter->ptp = NULL;
}

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
	info->gettimex64 = idpf_ptp_gettimex64;
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

	spin_lock_init(&adapter->ptp->read_dev_clk_lock);

	pci_dbg(adapter->pdev, "PTP init successful\n");

	return 0;

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

	if (ptp->clock)
		ptp_clock_unregister(ptp->clock);

	kfree(ptp);
	adapter->ptp = NULL;
}

// SPDX-License-Identifier: GPL-2.0+
/*
 * ipmi_poweroff.c
 *
 * MontaVista IPMI Poweroff extension to sys_reboot
 *
 * Author: MontaVista Software, Inc.
 *         Steven Dake <sdake@mvista.com>
 *         Corey Minyard <cminyard@mvista.com>
 *         source@mvista.com
 *
 * Copyright 2002,2004 MontaVista Software Inc.
 */

#define pr_fmt(fmt) "IPMI poweroff: " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/completion.h>
#include <linux/pm.h>
#include <linux/kdev_t.h>
#include <linux/ipmi.h>
#include <linux/ipmi_smi.h>

static void ipmi_po_smi_gone(int if_num);
static void ipmi_po_new_smi(int if_num, struct device *device);

/* Definitions for controlling power off (if the system supports it).  It
 * conveniently matches the IPMI chassis control values. */
#define IPMI_CHASSIS_POWER_DOWN		0	/* power down, the default. */
#define IPMI_CHASSIS_POWER_CYCLE	0x02	/* power cycle */

/* the IPMI data command */
static int poweroff_powercycle;

/* Which interface to use, -1 means the first we see. */
static int ifnum_to_use = -1;

/* Our local state. */
static int ready;
static struct ipmi_user *ipmi_user;
static int ipmi_ifnum;
static void (*specific_poweroff_func)(struct ipmi_user *user);

/* Holds the old poweroff function so we can restore it on removal. */
static void (*old_poweroff_func)(void);

static int set_param_ifnum(const char *val, const struct kernel_param *kp)
{
	int rv = param_set_int(val, kp);
	if (rv)
		return rv;
	if ((ifnum_to_use < 0) || (ifnum_to_use == ipmi_ifnum))
		return 0;

	ipmi_po_smi_gone(ipmi_ifnum);
	ipmi_po_new_smi(ifnum_to_use, NULL);
	return 0;
}

module_param_call(ifnum_to_use, set_param_ifnum, param_get_int,
		  &ifnum_to_use, 0644);
MODULE_PARM_DESC(ifnum_to_use, "The interface number to use for the watchdog "
		 "timer.  Setting to -1 defaults to the first registered "
		 "interface");

/* parameter definition to allow user to flag power cycle */
module_param(poweroff_powercycle, int, 0644);
MODULE_PARM_DESC(poweroff_powercycle,
		 " Set to non-zero to enable power cycle instead of power"
		 " down. Power cycle is contingent on hardware support,"
		 " otherwise it defaults back to power down.");

/* Stuff from the get device id command. */
static unsigned int mfg_id;
static unsigned int prod_id;
static unsigned char capabilities;
static unsigned char ipmi_version;

/*
 * We use our own messages for this operation, we don't let the system
 * allocate them, since we may be in a panic situation.  The whole
 * thing is single-threaded, anyway, so multiple messages are not
 * required.
 */
static atomic_t dummy_count = ATOMIC_INIT(0);
static void dummy_smi_free(struct ipmi_smi_msg *msg)
{
	atomic_dec(&dummy_count);
}
static void dummy_recv_free(struct ipmi_recv_msg *msg)
{
	atomic_dec(&dummy_count);
}
static struct ipmi_smi_msg halt_smi_msg = INIT_IPMI_SMI_MSG(dummy_smi_free);
static struct ipmi_recv_msg halt_recv_msg = INIT_IPMI_RECV_MSG(dummy_recv_free);


/*
 * Code to send a message and wait for the response.
 */

static void receive_handler(struct ipmi_recv_msg *recv_msg, void *handler_data)
{
	struct completion *comp = recv_msg->user_msg_data;

	if (comp)
		complete(comp);
}

static const struct ipmi_user_hndl ipmi_poweroff_handler = {
	.ipmi_recv_hndl = receive_handler
};


static int ipmi_request_wait_for_response(struct ipmi_user       *user,
					  struct ipmi_addr       *addr,
					  struct kernel_ipmi_msg *send_msg)
{
	int               rv;
	struct completion comp;

	init_completion(&comp);

	rv = ipmi_request_supply_msgs(user, addr, 0, send_msg, &comp,
				      &halt_smi_msg, &halt_recv_msg, 0);
	if (rv)
		return rv;

	wait_for_completion(&comp);

	return halt_recv_msg.msg.data[0];
}

/* Wait for message to complete, spinning. */
static int ipmi_request_in_rc_mode(struct ipmi_user       *user,
				   struct ipmi_addr       *addr,
				   struct kernel_ipmi_msg *send_msg)
{
	int rv;

	atomic_set(&dummy_count, 2);
	rv = ipmi_request_supply_msgs(user, addr, 0, send_msg, NULL,
				      &halt_smi_msg, &halt_recv_msg, 0);
	if (rv) {
		atomic_set(&dummy_count, 0);
		return rv;
	}

	/*
	 * Spin until our message is done.
	 */
	while (atomic_read(&dummy_count) > 0) {
		ipmi_poll_interface(user);
		cpu_relax();
	}

	return halt_recv_msg.msg.data[0];
}

/*
 * ATCA Support
 */

#define IPMI_NETFN_ATCA			0x2c
#define IPMI_ATCA_SET_POWER_CMD		0x11
#define IPMI_ATCA_GET_ADDR_INFO_CMD	0x01
#define IPMI_PICMG_ID			0

#define IPMI_NETFN_OEM				0x2e
#define IPMI_ATCA_PPS_GRACEFUL_RESTART		0x11
#define IPMI_ATCA_PPS_IANA			"\x00\x40\x0A"
#define IPMI_MOTOROLA_MANUFACTURER_ID		0x0000A1
#define IPMI_MOTOROLA_PPS_IPMC_PRODUCT_ID	0x0051

static void (*atca_oem_poweroff_hook)(struct ipmi_user *user);

static void pps_poweroff_atca(struct ipmi_user *user)
{
	struct ipmi_system_interface_addr smi_addr;
	struct kernel_ipmi_msg            send_msg;
	int                               rv;
	/*
	 * Configure IPMI address for local access
	 */
	smi_addr.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
	smi_addr.channel = IPMI_BMC_CHANNEL;
	smi_addr.lun = 0;

	pr_info("PPS powerdown hook used\n");

	send_msg.netfn = IPMI_NETFN_OEM;
	send_msg.cmd = IPMI_ATCA_PPS_GRACEFUL_RESTART;
	send_msg.data = IPMI_ATCA_PPS_IANA;
	send_msg.data_len = 3;
	rv = ipmi_request_in_rc_mode(user,
				     (struct ipmi_addr *) &smi_addr,
				     &send_msg);
	if (rv && rv != IPMI_UNKNOWN_ERR_COMPLETION_CODE)
		pr_err("Unable to send ATCA, IPMI error 0x%x\n", rv);

	return;
}

static int ipmi_atca_detect(struct ipmi_user *user)
{
	struct ipmi_system_interface_addr smi_addr;
	struct kernel_ipmi_msg            send_msg;
	int                               rv;
	unsigned char                     data[1];

	/*
	 * Configure IPMI address for local access
	 */
	smi_addr.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
	smi_addr.channel = IPMI_BMC_CHANNEL;
	smi_addr.lun = 0;

	/*
	 * Use get address info to check and see if we are ATCA
	 */
	send_msg.netfn = IPMI_NETFN_ATCA;
	send_msg.cmd = IPMI_ATCA_GET_ADDR_INFO_CMD;
	data[0] = IPMI_PICMG_ID;
	send_msg.data = data;
	send_msg.data_len = sizeof(data);
	rv = ipmi_request_wait_for_response(user,
					    (struct ipmi_addr *) &smi_addr,
					    &send_msg);

	pr_info("ATCA Detect mfg 0x%X prod 0x%X\n", mfg_id, prod_id);
	if ((mfg_id == IPMI_MOTOROLA_MANUFACTURER_ID)
	    && (prod_id == IPMI_MOTOROLA_PPS_IPMC_PRODUCT_ID)) {
		pr_info("Installing Pigeon Point Systems Poweroff Hook\n");
		atca_oem_poweroff_hook = pps_poweroff_atca;
	}
	return !rv;
}

static void ipmi_poweroff_atca(struct ipmi_user *user)
{
	struct ipmi_system_interface_addr smi_addr;
	struct kernel_ipmi_msg            send_msg;
	int                               rv;
	unsigned char                     data[4];

	/*
	 * Configure IPMI address for local access
	 */
	smi_addr.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
	smi_addr.channel = IPMI_BMC_CHANNEL;
	smi_addr.lun = 0;

	pr_info("Powering down via ATCA power command\n");

	/*
	 * Power down
	 */
	send_msg.netfn = IPMI_NETFN_ATCA;
	send_msg.cmd = IPMI_ATCA_SET_POWER_CMD;
	data[0] = IPMI_PICMG_ID;
	data[1] = 0; /* FRU id */
	data[2] = 0; /* Power Level */
	data[3] = 0; /* Don't change saved presets */
	send_msg.data = data;
	send_msg.data_len = sizeof(data);
	rv = ipmi_request_in_rc_mode(user,
				     (struct ipmi_addr *) &smi_addr,
				     &send_msg);
	/*
	 * At this point, the system may be shutting down, and most
	 * serial drivers (if used) will have interrupts turned off
	 * it may be better to ignore IPMI_UNKNOWN_ERR_COMPLETION_CODE
	 * return code
	 */
	if (rv && rv != IPMI_UNKNOWN_ERR_COMPLETION_CODE) {
		pr_err("Unable to send ATCA powerdown message, IPMI error 0x%x\n",
		       rv);
		goto out;
	}

	if (atca_oem_poweroff_hook)
		atca_oem_poweroff_hook(user);
 out:
	return;
}

/*
 * CPI1 Support
 */

#define IPMI_NETFN_OEM_1				0xf8
#define OEM_GRP_CMD_SET_RESET_STATE		0x84
#define OEM_GRP_CMD_SET_POWER_STATE		0x82
#define IPMI_NETFN_OEM_8				0xf8
#define OEM_GRP_CMD_REQUEST_HOTSWAP_CTRL	0x80
#define OEM_GRP_CMD_GET_SLOT_GA			0xa3
#define IPMI_NETFN_SENSOR_EVT			0x10
#define IPMI_CMD_GET_EVENT_RECEIVER		0x01

#define IPMI_CPI1_PRODUCT_ID		0x000157
#define IPMI_CPI1_MANUFACTURER_ID	0x0108

static int ipmi_cpi1_detect(struct ipmi_user *user)
{
	return ((mfg_id == IPMI_CPI1_MANUFACTURER_ID)
		&& (prod_id == IPMI_CPI1_PRODUCT_ID));
}

static void ipmi_poweroff_cpi1(struct ipmi_user *user)
{
	struct ipmi_system_interface_addr smi_addr;
	struct ipmi_ipmb_addr             ipmb_addr;
	struct kernel_ipmi_msg            send_msg;
	int                               rv;
	unsigned char                     data[1];
	int                               slot;
	unsigned char                     hotswap_ipmb;
	unsigned char                     aer_addr;
	unsigned char                     aer_lun;

	/*
	 * Configure IPMI address for local access
	 */
	smi_addr.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
	smi_addr.channel = IPMI_BMC_CHANNEL;
	smi_addr.lun = 0;

	pr_info("Powering down via CPI1 power command\n");

	/*
	 * Get IPMI ipmb address
	 */
	send_msg.netfn = IPMI_NETFN_OEM_8 >> 2;
	send_msg.cmd = OEM_GRP_CMD_GET_SLOT_GA;
	send_msg.data = NULL;
	send_msg.data_len = 0;
	rv = ipmi_request_in_rc_mode(user,
				     (struct ipmi_addr *) &smi_addr,
				     &send_msg);
	if (rv)
		goto out;
	slot = halt_recv_msg.msg.data[1];
	hotswap_ipmb = (slot > 9) ? (0xb0 + 2 * slot) : (0xae + 2 * slot);

	/*
	 * Get active event receiver
	 */
	send_msg.netfn = IPMI_NETFN_SENSOR_EVT >> 2;
	send_msg.cmd = IPMI_CMD_GET_EVENT_RECEIVER;
	send_msg.data = NULL;
	send_msg.data_len = 0;
	rv = ipmi_request_in_rc_mode(user,
				     (struct ipmi_addr *) &smi_addr,
				     &send_msg);
	if (rv)
		goto out;
	aer_addr = halt_recv_msg.msg.data[1];
	aer_lun = halt_recv_msg.msg.data[2];

	/*
	 * Setup IPMB address target instead of local target
	 */
	ipmb_addr.addr_type = IPMI_IPMB_ADDR_TYPE;
	ipmb_addr.channel = 0;
	ipmb_addr.slave_addr = aer_addr;
	ipmb_addr.lun = aer_lun;

	/*
	 * Send request hotswap control to remove blade from dpv
	 */
	send_msg.netfn = IPMI_NETFN_OEM_8 >> 2;
	send_msg.cmd = OEM_GRP_CMD_REQUEST_HOTSWAP_CTRL;
	send_msg.data = &hotswap_ipmb;
	send_msg.data_len = 1;
	ipmi_request_in_rc_mode(user,
				(struct ipmi_addr *) &ipmb_addr,
				&send_msg);

	/*
	 * Set reset asserted
	 */
	send_msg.netfn = IPMI_NETFN_OEM_1 >> 2;
	send_msg.cmd = OEM_GRP_CMD_SET_RESET_STATE;
	send_msg.data = data;
	data[0] = 1; /* Reset asserted state */
	send_msg.data_len = 1;
	rv = ipmi_request_in_rc_mode(user,
				     (struct ipmi_addr *) &smi_addr,
				     &send_msg);
	if (rv)
		goto out;

	/*
	 * Power down
	 */
	send_msg.netfn = IPMI_NETFN_OEM_1 >> 2;
	send_msg.cmd = OEM_GRP_CMD_SET_POWER_STATE;
	send_msg.data = data;
	data[0] = 1; /* Power down state */
	send_msg.data_len = 1;
	rv = ipmi_request_in_rc_mode(user,
				     (struct ipmi_addr *) &smi_addr,
				     &send_msg);
	if (rv)
		goto out;

 out:
	return;
}

/*
 * ipmi_dell_chassis_detect()
 * Dell systems with IPMI < 1.5 don't set the chassis capability bit
 * but they can handle a chassis poweroff or powercycle command.
 */

#define DELL_IANA_MFR_ID {0xA2, 0x02, 0x00}
static int ipmi_dell_chassis_detect(struct ipmi_user *user)
{
	const char ipmi_version_major = ipmi_version & 0xF;
	const char ipmi_version_minor = (ipmi_version >> 4) & 0xF;
	const char mfr[3] = DELL_IANA_MFR_ID;
	if (!memcmp(mfr, &mfg_id, sizeof(mfr)) &&
	    ipmi_version_major <= 1 &&
	    ipmi_version_minor < 5)
		return 1;
	return 0;
}

/*
 * ipmi_hp_chassis_detect()
 * HP PA-RISC servers rp3410/rp3440, the C8000 workstation and the rx2600 and
 * zx6000 machines support IPMI vers 1 and don't set the chassis capability bit
 * but they can handle a chassis poweroff or powercycle command.
 */

#define HP_IANA_MFR_ID 0x0b
#define HP_BMC_PROD_ID 0x8201
static int ipmi_hp_chassis_detect(struct ipmi_user *user)
{
	if (mfg_id == HP_IANA_MFR_ID
		&& prod_id == HP_BMC_PROD_ID
		&& ipmi_version == 1)
		return 1;
	return 0;
}

/*
 * Standard chassis support
 */

#define IPMI_NETFN_CHASSIS_REQUEST	0
#define IPMI_CHASSIS_CONTROL_CMD	0x02

static int ipmi_chassis_detect(struct ipmi_user *user)
{
	/* Chassis support, use it. */
	return (capabilities & 0x80);
}

static void ipmi_poweroff_chassis(struct ipmi_user *user)
{
	struct ipmi_system_interface_addr smi_addr;
	struct kernel_ipmi_msg            send_msg;
	int                               rv;
	unsigned char                     data[1];

	/*
	 * Configure IPMI address for local access
	 */
	smi_addr.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
	smi_addr.channel = IPMI_BMC_CHANNEL;
	smi_addr.lun = 0;

 powercyclefailed:
	pr_info("Powering %s via IPMI chassis control command\n",
		(poweroff_powercycle ? "cycle" : "down"));

	/*
	 * Power down
	 */
	send_msg.netfn = IPMI_NETFN_CHASSIS_REQUEST;
	send_msg.cmd = IPMI_CHASSIS_CONTROL_CMD;
	if (poweroff_powercycle)
		data[0] = IPMI_CHASSIS_POWER_CYCLE;
	else
		data[0] = IPMI_CHASSIS_POWER_DOWN;
	send_msg.data = data;
	send_msg.data_len = sizeof(data);
	rv = ipmi_request_in_rc_mode(user,
				     (struct ipmi_addr *) &smi_addr,
				     &send_msg);
	if (rv) {
		if (poweroff_powercycle) {
			/* power cycle failed, default to power down */
			pr_err("Unable to send chassis power cycle message, IPMI error 0x%x\n",
			       rv);
			poweroff_powercycle = 0;
			goto powercyclefailed;
		}

		pr_err("Unable to send chassis power down message, IPMI error 0x%x\n",
		       rv);
	}
}


/* Table of possible power off functions. */
struct poweroff_function {
	char *platform_type;
	int  (*detect)(struct ipmi_user *user);
	void (*poweroff_func)(struct ipmi_user *user);
};

static struct poweroff_function poweroff_functions[] = {
	{ .platform_type	= "ATCA",
	  .detect		= ipmi_atca_detect,
	  .poweroff_func	= ipmi_poweroff_atca },
	{ .platform_type	= "CPI1",
	  .detect		= ipmi_cpi1_detect,
	  .poweroff_func	= ipmi_poweroff_cpi1 },
	{ .platform_type	= "chassis",
	  .detect		= ipmi_dell_chassis_detect,
	  .poweroff_func	= ipmi_poweroff_chassis },
	{ .platform_type	= "chassis",
	  .detect		= ipmi_hp_chassis_detect,
	  .poweroff_func	= ipmi_poweroff_chassis },
	/* Chassis should generally be last, other things should override
	   it. */
	{ .platform_type	= "chassis",
	  .detect		= ipmi_chassis_detect,
	  .poweroff_func	= ipmi_poweroff_chassis },
};
#define NUM_PO_FUNCS ARRAY_SIZE(poweroff_functions)


/* Called on a powerdown request. */
static void ipmi_poweroff_function(void)
{
	if (!ready)
		return;

	/* Use run-to-completion mode, since interrupts may be off. */
	specific_poweroff_func(ipmi_user);
}

/* Wait for an IPMI interface to be installed, the first one installed
   will be grabbed by this code and used to perform the powerdown. */
static void ipmi_po_new_smi(int if_num, struct device *device)
{
	struct ipmi_system_interface_addr smi_addr;
	struct kernel_ipmi_msg            send_msg;
	int                               rv;
	int                               i;

	if (ready)
		return;

	if ((ifnum_to_use >= 0) && (ifnum_to_use != if_num))
		return;

	rv = ipmi_create_user(if_num, &ipmi_poweroff_handler, NULL,
			      &ipmi_user);
	if (rv) {
		pr_err("could not create IPMI user, error %d\n", rv);
		return;
	}

	ipmi_ifnum = if_num;

	/*
	 * Do a get device ide and store some results, since this is
	 * used by several functions.
	 */
	smi_addr.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
	smi_addr.channel = IPMI_BMC_CHANNEL;
	smi_addr.lun = 0;

	send_msg.netfn = IPMI_NETFN_APP_REQUEST;
	send_msg.cmd = IPMI_GET_DEVICE_ID_CMD;
	send_msg.data = NULL;
	send_msg.data_len = 0;
	rv = ipmi_request_wait_for_response(ipmi_user,
					    (struct ipmi_addr *) &smi_addr,
					    &send_msg);
	if (rv) {
		pr_err("Unable to send IPMI get device id info, IPMI error 0x%x\n",
		       rv);
		goto out_err;
	}

	if (halt_recv_msg.msg.data_len < 12) {
		pr_err("(chassis) IPMI get device id info too short, was %d bytes, needed %d bytes\n",
		       halt_recv_msg.msg.data_len, 12);
		goto out_err;
	}

	mfg_id = (halt_recv_msg.msg.data[7]
		  | (halt_recv_msg.msg.data[8] << 8)
		  | (halt_recv_msg.msg.data[9] << 16));
	prod_id = (halt_recv_msg.msg.data[10]
		   | (halt_recv_msg.msg.data[11] << 8));
	capabilities = halt_recv_msg.msg.data[6];
	ipmi_version = halt_recv_msg.msg.data[5];


	/* Scan for a poweroff method */
	for (i = 0; i < NUM_PO_FUNCS; i++) {
		if (poweroff_functions[i].detect(ipmi_user))
			goto found;
	}

 out_err:
	pr_err("Unable to find a poweroff function that will work, giving up\n");
	ipmi_destroy_user(ipmi_user);
	return;

 found:
	pr_info("Found a %s style poweroff function\n",
		poweroff_functions[i].platform_type);
	specific_poweroff_func = poweroff_functions[i].poweroff_func;
	old_poweroff_func = pm_power_off;
	pm_power_off = ipmi_poweroff_function;
	ready = 1;
}

static void ipmi_po_smi_gone(int if_num)
{
	if (!ready)
		return;

	if (ipmi_ifnum != if_num)
		return;

	ready = 0;
	ipmi_destroy_user(ipmi_user);
	pm_power_off = old_poweroff_func;
}

static struct ipmi_smi_watcher smi_watcher = {
	.owner    = THIS_MODULE,
	.new_smi  = ipmi_po_new_smi,
	.smi_gone = ipmi_po_smi_gone
};


#ifdef CONFIG_PROC_FS
#include <linux/sysctl.h>

static struct ctl_table ipmi_table[] = {
	{ .procname	= "poweroff_powercycle",
	  .data		= &poweroff_powercycle,
	  .maxlen	= sizeof(poweroff_powercycle),
	  .mode		= 0644,
	  .proc_handler	= proc_dointvec },
};

static struct ctl_table_header *ipmi_table_header;
#endif /* CONFIG_PROC_FS */

/*
 * Startup and shutdown functions.
 */
static int __init ipmi_poweroff_init(void)
{
	int rv;

	pr_info("Copyright (C) 2004 MontaVista Software - IPMI Powerdown via sys_reboot\n");

	if (poweroff_powercycle)
		pr_info("Power cycle is enabled\n");

#ifdef CONFIG_PROC_FS
	ipmi_table_header = register_sysctl("dev/ipmi", ipmi_table);
	if (!ipmi_table_header) {
		pr_err("Unable to register powercycle sysctl\n");
		rv = -ENOMEM;
		goto out_err;
	}
#endif

	rv = ipmi_smi_watcher_register(&smi_watcher);

#ifdef CONFIG_PROC_FS
	if (rv) {
		unregister_sysctl_table(ipmi_table_header);
		pr_err("Unable to register SMI watcher: %d\n", rv);
		goto out_err;
	}

 out_err:
#endif
	return rv;
}

#ifdef MODULE
static void __exit ipmi_poweroff_cleanup(void)
{
	int rv;

#ifdef CONFIG_PROC_FS
	unregister_sysctl_table(ipmi_table_header);
#endif

	ipmi_smi_watcher_unregister(&smi_watcher);

	if (ready) {
		rv = ipmi_destroy_user(ipmi_user);
		if (rv)
			pr_err("could not cleanup the IPMI user: 0x%x\n", rv);
		pm_power_off = old_poweroff_func;
	}
}
module_exit(ipmi_poweroff_cleanup);
#endif

module_init(ipmi_poweroff_init);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Corey Minyard <minyard@mvista.com>");
MODULE_DESCRIPTION("IPMI Poweroff extension to sys_reboot");

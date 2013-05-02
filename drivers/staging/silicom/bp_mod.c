/******************************************************************************/
/*                                                                            */
/* Bypass Control utility, Copyright (c) 2005-20011 Silicom                   */
/*                                                                            */
/* This program is free software; you can redistribute it and/or modify       */
/* it under the terms of the GNU General Public License as published by       */
/* the Free Software Foundation, located in the file LICENSE.                 */
/*  Copyright(c) 2007 - 2009 Intel Corporation. All rights reserved.          */
/*                                                                            */
/*                                                                            */
/******************************************************************************/

#include <linux/kernel.h>	/* We're doing kernel work */
#include <linux/module.h>	/* Specifically, a module */
#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/rcupdate.h>
#include <linux/etherdevice.h>

#include <linux/uaccess.h>	/* for get_user and put_user */
#include <linux/sched.h>
#include <linux/ethtool.h>
#include <linux/proc_fs.h>

#include "bp_ioctl.h"
#include "bp_mod.h"
#include "bypass.h"
#include "libbp_sd.h"

#define SUCCESS 0
#define BP_MOD_VER  "9.0.4"
#define BP_MOD_DESCR "Silicom Bypass-SD Control driver"
#define BP_SYNC_FLAG 1

static int Device_Open = 0;
static int major_num = 0;

MODULE_AUTHOR("Anna Lukin, annal@silicom.co.il");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(BP_MOD_DESCR);
MODULE_VERSION(BP_MOD_VER);
spinlock_t bpvm_lock;

#define lock_bpctl() 					\
if (down_interruptible(&bpctl_sema)) {			\
	return -ERESTARTSYS;				\
}							\

#define unlock_bpctl() 					\
	up(&bpctl_sema);

/* Media Types */
typedef enum {
	bp_copper = 0,
	bp_fiber,
	bp_cx4,
	bp_none,
} bp_media_type;

struct pfs_unit_sd {
	struct proc_dir_entry *proc_entry;
	char proc_name[32];
};

struct bypass_pfs_sd {
	char dir_name[32];
	struct proc_dir_entry *bypass_entry;
	struct pfs_unit_sd bypass_info;
	struct pfs_unit_sd bypass_slave;
	struct pfs_unit_sd bypass_caps;
	struct pfs_unit_sd wd_set_caps;
	struct pfs_unit_sd bypass;
	struct pfs_unit_sd bypass_change;
	struct pfs_unit_sd bypass_wd;
	struct pfs_unit_sd wd_expire_time;
	struct pfs_unit_sd reset_bypass_wd;
	struct pfs_unit_sd dis_bypass;
	struct pfs_unit_sd bypass_pwup;
	struct pfs_unit_sd bypass_pwoff;
	struct pfs_unit_sd std_nic;
	struct pfs_unit_sd tap;
	struct pfs_unit_sd dis_tap;
	struct pfs_unit_sd tap_pwup;
	struct pfs_unit_sd tap_change;
	struct pfs_unit_sd wd_exp_mode;
	struct pfs_unit_sd wd_autoreset;
	struct pfs_unit_sd tpl;

};

typedef struct _bpctl_dev {
	char *name;
	char *desc;
	struct pci_dev *pdev;	/* PCI device */
	struct net_device *ndev;	/* net device */
	unsigned long mem_map;
	uint8_t bus;
	uint8_t slot;
	uint8_t func;
	u_int32_t device;
	u_int32_t vendor;
	u_int32_t subvendor;
	u_int32_t subdevice;
	int ifindex;
	uint32_t bp_caps;
	uint32_t bp_caps_ex;
	uint8_t bp_fw_ver;
	int bp_ext_ver;
	int wdt_status;
	unsigned long bypass_wdt_on_time;
	uint32_t bypass_timer_interval;
	struct timer_list bp_timer;
	uint32_t reset_time;
	uint8_t bp_status_un;
	atomic_t wdt_busy;
	bp_media_type media_type;
	int bp_tpl_flag;
	struct timer_list bp_tpl_timer;
	spinlock_t bypass_wr_lock;
	int bp_10g;
	int bp_10gb;
	int bp_fiber5;
	int bp_10g9;
	int bp_i80;
	int bp_540;
	int (*hard_start_xmit_save) (struct sk_buff *skb,
				     struct net_device *dev);
	const struct net_device_ops *old_ops;
	struct net_device_ops new_ops;
	int bp_self_test_flag;
	char *bp_tx_data;
	struct bypass_pfs_sd bypass_pfs_set;

} bpctl_dev_t;

static bpctl_dev_t *bpctl_dev_arr;

static struct semaphore bpctl_sema;
static int device_num = 0;

static int get_dev_idx(int ifindex);
static bpctl_dev_t *get_master_port_fn(bpctl_dev_t *pbpctl_dev);
static int disc_status(bpctl_dev_t *pbpctl_dev);
static int bypass_status(bpctl_dev_t *pbpctl_dev);
static int wdt_timer(bpctl_dev_t *pbpctl_dev, int *time_left);
static bpctl_dev_t *get_status_port_fn(bpctl_dev_t *pbpctl_dev);
static void if_scan_init(void);

int bypass_proc_create_dev_sd(bpctl_dev_t *pbp_device_block);
int bypass_proc_remove_dev_sd(bpctl_dev_t *pbp_device_block);
int bp_proc_create(void);

int is_bypass_fn(bpctl_dev_t *pbpctl_dev);
int get_dev_idx_bsf(int bus, int slot, int func);

static unsigned long str_to_hex(char *p);
static int bp_device_event(struct notifier_block *unused,
			   unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;
	static bpctl_dev_t *pbpctl_dev = NULL, *pbpctl_dev_m = NULL;
	int dev_num = 0, ret = 0, ret_d = 0, time_left = 0;
	/* printk("BP_PROC_SUPPORT event =%d %s %d\n", event,dev->name, dev->ifindex ); */
	/* return NOTIFY_DONE; */
	if (!dev)
		return NOTIFY_DONE;
	if (event == NETDEV_REGISTER) {
		{
			struct ethtool_drvinfo drvinfo;
			char cbuf[32];
			char *buf = NULL;
			char res[10];
			int i = 0, ifindex, idx_dev = 0;
			int bus = 0, slot = 0, func = 0;
			ifindex = dev->ifindex;

			memset(res, 0, 10);
			memset(&drvinfo, 0, sizeof(struct ethtool_drvinfo));

			if (dev->ethtool_ops && dev->ethtool_ops->get_drvinfo) {
				memset(&drvinfo, 0, sizeof(drvinfo));
				dev->ethtool_ops->get_drvinfo(dev, &drvinfo);
			} else
				return NOTIFY_DONE;
			if (!drvinfo.bus_info)
				return NOTIFY_DONE;
			if (!strcmp(drvinfo.bus_info, "N/A"))
				return NOTIFY_DONE;
			memcpy(&cbuf, drvinfo.bus_info, 32);
			buf = &cbuf[0];

			while (*buf++ != ':') ;
			for (i = 0; i < 10; i++, buf++) {
				if (*buf == ':')
					break;
				res[i] = *buf;

			}
			buf++;
			bus = str_to_hex(res);
			memset(res, 0, 10);

			for (i = 0; i < 10; i++, buf++) {
				if (*buf == '.')
					break;
				res[i] = *buf;

			}
			buf++;
			slot = str_to_hex(res);
			func = str_to_hex(buf);
			idx_dev = get_dev_idx_bsf(bus, slot, func);

			if (idx_dev != -1) {

				bpctl_dev_arr[idx_dev].ifindex = ifindex;
				bpctl_dev_arr[idx_dev].ndev = dev;

				bypass_proc_remove_dev_sd(&bpctl_dev_arr
							  [idx_dev]);
				bypass_proc_create_dev_sd(&bpctl_dev_arr
							  [idx_dev]);

			}

		}
		return NOTIFY_DONE;

	}
	if (event == NETDEV_UNREGISTER) {
		int idx_dev = 0;
		for (idx_dev = 0;
		     ((bpctl_dev_arr[idx_dev].pdev != NULL)
		      && (idx_dev < device_num)); idx_dev++) {
			if (bpctl_dev_arr[idx_dev].ndev == dev) {
				bypass_proc_remove_dev_sd(&bpctl_dev_arr
							  [idx_dev]);
				bpctl_dev_arr[idx_dev].ndev = NULL;

				return NOTIFY_DONE;

			}

		}
		return NOTIFY_DONE;
	}
	if (event == NETDEV_CHANGENAME) {
		int idx_dev = 0;
		for (idx_dev = 0;
		     ((bpctl_dev_arr[idx_dev].pdev != NULL)
		      && (idx_dev < device_num)); idx_dev++) {
			if (bpctl_dev_arr[idx_dev].ndev == dev) {
				bypass_proc_remove_dev_sd(&bpctl_dev_arr
							  [idx_dev]);
				bypass_proc_create_dev_sd(&bpctl_dev_arr
							  [idx_dev]);

				return NOTIFY_DONE;

			}

		}
		return NOTIFY_DONE;

	}

	switch (event) {

	case NETDEV_CHANGE:{
			if (netif_carrier_ok(dev))
				return NOTIFY_DONE;

			if (((dev_num = get_dev_idx(dev->ifindex)) == -1) ||
			    (!(pbpctl_dev = &bpctl_dev_arr[dev_num])))
				return NOTIFY_DONE;

			if ((is_bypass_fn(pbpctl_dev)) == 1)
				pbpctl_dev_m = pbpctl_dev;
			else
				pbpctl_dev_m = get_master_port_fn(pbpctl_dev);
			if (!pbpctl_dev_m)
				return NOTIFY_DONE;
			ret = bypass_status(pbpctl_dev_m);
			if (ret == 1)
				printk("bpmod: %s is in the Bypass mode now",
				       dev->name);
			ret_d = disc_status(pbpctl_dev_m);
			if (ret_d == 1)
				printk
				    ("bpmod: %s is in the Disconnect mode now",
				     dev->name);
			if (ret || ret_d) {
				wdt_timer(pbpctl_dev_m, &time_left);
				if (time_left == -1)
					printk("; WDT has expired");
				printk(".\n");

			}
			return NOTIFY_DONE;

		}

	default:
		return NOTIFY_DONE;

	}
	return NOTIFY_DONE;

}

static struct notifier_block bp_notifier_block = {
	.notifier_call = bp_device_event,
};

static int device_open(struct inode *inode, struct file *file)
{
#ifdef DEBUG
	printk("device_open(%p)\n", file);
#endif
	Device_Open++;
/*
* Initialize the message
*/
	return SUCCESS;
}

static int device_release(struct inode *inode, struct file *file)
{
#ifdef DEBUG
	printk("device_release(%p,%p)\n", inode, file);
#endif
	Device_Open--;
	return SUCCESS;
}

int is_bypass_fn(bpctl_dev_t *pbpctl_dev);
int wdt_time_left(bpctl_dev_t *pbpctl_dev);

static void write_pulse(bpctl_dev_t *pbpctl_dev,
			unsigned int ctrl_ext,
			unsigned char value, unsigned char len)
{
	unsigned char ctrl_val = 0;
	unsigned int i = len;
	unsigned int ctrl = 0;
	bpctl_dev_t *pbpctl_dev_c = NULL;

	if (pbpctl_dev->bp_i80)
		ctrl = BPCTL_READ_REG(pbpctl_dev, CTRL_EXT);
	if (pbpctl_dev->bp_540)
		ctrl = BP10G_READ_REG(pbpctl_dev, ESDP);

	if (pbpctl_dev->bp_10g9) {
		if (!(pbpctl_dev_c = get_status_port_fn(pbpctl_dev)))
			return;
		ctrl = BP10G_READ_REG(pbpctl_dev_c, ESDP);
	}

	while (i--) {
		ctrl_val = (value >> i) & 0x1;
		if (ctrl_val) {
			if (pbpctl_dev->bp_10g9) {

				/* To start management : MCLK 1, MDIO 1, output */
				/* DATA 1 CLK 1 */
				/*BP10G_WRITE_REG(pbpctl_dev, I2CCTL, (ctrl_ext|BP10G_MCLK_DATA_OUT9|BP10G_MDIO_DATA_OUT9)); */
				BP10G_WRITE_REG(pbpctl_dev, I2CCTL,
						ctrl_ext |
						BP10G_MDIO_DATA_OUT9);
				BP10G_WRITE_REG(pbpctl_dev_c, ESDP,
						(ctrl | BP10G_MCLK_DATA_OUT9 |
						 BP10G_MCLK_DIR_OUT9));

			} else if (pbpctl_dev->bp_fiber5) {
				BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL, (ctrl_ext |
								      BPCTLI_CTRL_EXT_MCLK_DIR5
								      |
								      BPCTLI_CTRL_EXT_MDIO_DIR5
								      |
								      BPCTLI_CTRL_EXT_MDIO_DATA5
								      |
								      BPCTLI_CTRL_EXT_MCLK_DATA5));

			} else if (pbpctl_dev->bp_i80) {
				BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL, (ctrl_ext |
								      BPCTLI_CTRL_EXT_MDIO_DIR80
								      |
								      BPCTLI_CTRL_EXT_MDIO_DATA80));

				BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, (ctrl |
									  BPCTLI_CTRL_EXT_MCLK_DIR80
									  |
									  BPCTLI_CTRL_EXT_MCLK_DATA80));

			} else if (pbpctl_dev->bp_540) {
				BP10G_WRITE_REG(pbpctl_dev, ESDP, (ctrl |
								   BP540_MDIO_DIR
								   |
								   BP540_MDIO_DATA
								   |
								   BP540_MCLK_DIR
								   |
								   BP540_MCLK_DATA));

			} else if (pbpctl_dev->bp_10gb) {
				BP10GB_WRITE_REG(pbpctl_dev, MISC_REG_SPIO,
						 (ctrl_ext | BP10GB_MDIO_SET |
						  BP10GB_MCLK_SET) &
						 ~(BP10GB_MCLK_DIR |
						   BP10GB_MDIO_DIR |
						   BP10GB_MDIO_CLR |
						   BP10GB_MCLK_CLR));

			} else if (!pbpctl_dev->bp_10g)
				/* To start management : MCLK 1, MDIO 1, output */
				BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT,
						   (ctrl_ext |
						    BPCTLI_CTRL_EXT_MCLK_DIR |
						    BPCTLI_CTRL_EXT_MDIO_DIR |
						    BPCTLI_CTRL_EXT_MDIO_DATA |
						    BPCTLI_CTRL_EXT_MCLK_DATA));
			else {

				/* To start management : MCLK 1, MDIO 1, output*/
				BP10G_WRITE_REG(pbpctl_dev, EODSDP,
						(ctrl_ext | BP10G_MCLK_DATA_OUT
						 | BP10G_MDIO_DATA_OUT));

			}

			usec_delay(PULSE_TIME);
			if (pbpctl_dev->bp_10g9) {

				/*BP10G_WRITE_REG(pbpctl_dev, I2CCTL, ((ctrl_ext|BP10G_MDIO_DATA_OUT9)&~(BP10G_MCLK_DATA_OUT9))); */
				/* DATA 1 CLK 0 */
				BP10G_WRITE_REG(pbpctl_dev, I2CCTL,
						ctrl_ext |
						BP10G_MDIO_DATA_OUT9);
				BP10G_WRITE_REG(pbpctl_dev_c, ESDP,
						(ctrl | BP10G_MCLK_DIR_OUT9) &
						~BP10G_MCLK_DATA_OUT9);

			} else if (pbpctl_dev->bp_fiber5) {
				BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL,
						   ((ctrl_ext |
						     BPCTLI_CTRL_EXT_MCLK_DIR5 |
						     BPCTLI_CTRL_EXT_MDIO_DIR5 |
						     BPCTLI_CTRL_EXT_MDIO_DATA5)
						    &
						    ~
						    (BPCTLI_CTRL_EXT_MCLK_DATA5)));

			} else if (pbpctl_dev->bp_i80) {
				BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL, (ctrl_ext |
								      BPCTLI_CTRL_EXT_MDIO_DIR80
								      |
								      BPCTLI_CTRL_EXT_MDIO_DATA80));
				BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT,
						   ((ctrl |
						     BPCTLI_CTRL_EXT_MCLK_DIR80)
						    &
						    ~
						    (BPCTLI_CTRL_EXT_MCLK_DATA80)));

			} else if (pbpctl_dev->bp_540) {
				BP10G_WRITE_REG(pbpctl_dev, ESDP,
						(ctrl | BP540_MDIO_DIR |
						 BP540_MDIO_DATA |
						 BP540_MCLK_DIR) &
						~(BP540_MCLK_DATA));

			} else if (pbpctl_dev->bp_10gb) {

				BP10GB_WRITE_REG(pbpctl_dev, MISC_REG_SPIO,
						 (ctrl_ext | BP10GB_MDIO_SET |
						  BP10GB_MCLK_CLR) &
						 ~(BP10GB_MCLK_DIR |
						   BP10GB_MDIO_DIR |
						   BP10GB_MDIO_CLR |
						   BP10GB_MCLK_SET));

			} else if (!pbpctl_dev->bp_10g)

				BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT,
						   ((ctrl_ext |
						     BPCTLI_CTRL_EXT_MCLK_DIR |
						     BPCTLI_CTRL_EXT_MDIO_DIR |
						     BPCTLI_CTRL_EXT_MDIO_DATA)
						    &
						    ~
						    (BPCTLI_CTRL_EXT_MCLK_DATA)));
			else {

				BP10G_WRITE_REG(pbpctl_dev, EODSDP,
						((ctrl_ext |
						  BP10G_MDIO_DATA_OUT) &
						 ~(BP10G_MCLK_DATA_OUT)));
			}

			usec_delay(PULSE_TIME);

		} else {
			if (pbpctl_dev->bp_10g9) {
				/* DATA 0 CLK 1 */
				/*BP10G_WRITE_REG(pbpctl_dev, I2CCTL, ((ctrl_ext|BP10G_MCLK_DATA_OUT9)&~BP10G_MDIO_DATA_OUT9)); */
				BP10G_WRITE_REG(pbpctl_dev, I2CCTL,
						(ctrl_ext &
						 ~BP10G_MDIO_DATA_OUT9));
				BP10G_WRITE_REG(pbpctl_dev_c, ESDP,
						(ctrl | BP10G_MCLK_DATA_OUT9 |
						 BP10G_MCLK_DIR_OUT9));

			} else if (pbpctl_dev->bp_fiber5) {
				BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL,
						   ((ctrl_ext |
						     BPCTLI_CTRL_EXT_MCLK_DIR5 |
						     BPCTLI_CTRL_EXT_MDIO_DIR5 |
						     BPCTLI_CTRL_EXT_MCLK_DATA5)
						    &
						    ~
						    (BPCTLI_CTRL_EXT_MDIO_DATA5)));

			} else if (pbpctl_dev->bp_i80) {
				BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL,
						   ((ctrl_ext |
						     BPCTLI_CTRL_EXT_MDIO_DIR80)
						    &
						    ~
						    (BPCTLI_CTRL_EXT_MDIO_DATA80)));
				BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT,
						   (ctrl |
						    BPCTLI_CTRL_EXT_MCLK_DIR80 |
						    BPCTLI_CTRL_EXT_MCLK_DATA80));

			} else if (pbpctl_dev->bp_540) {
				BP10G_WRITE_REG(pbpctl_dev, ESDP,
						((ctrl | BP540_MCLK_DIR |
						  BP540_MCLK_DATA |
						  BP540_MDIO_DIR) &
						 ~(BP540_MDIO_DATA)));

			} else if (pbpctl_dev->bp_10gb) {
				BP10GB_WRITE_REG(pbpctl_dev, MISC_REG_SPIO,
						 (ctrl_ext | BP10GB_MDIO_CLR |
						  BP10GB_MCLK_SET) &
						 ~(BP10GB_MCLK_DIR |
						   BP10GB_MDIO_DIR |
						   BP10GB_MDIO_SET |
						   BP10GB_MCLK_CLR));

			} else if (!pbpctl_dev->bp_10g)

				BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT,
						   ((ctrl_ext |
						     BPCTLI_CTRL_EXT_MCLK_DIR |
						     BPCTLI_CTRL_EXT_MDIO_DIR |
						     BPCTLI_CTRL_EXT_MCLK_DATA)
						    &
						    ~
						    (BPCTLI_CTRL_EXT_MDIO_DATA)));
			else {

				BP10G_WRITE_REG(pbpctl_dev, EODSDP,
						((ctrl_ext |
						  BP10G_MCLK_DATA_OUT) &
						 ~BP10G_MDIO_DATA_OUT));

			}
			usec_delay(PULSE_TIME);
			if (pbpctl_dev->bp_10g9) {
				/* DATA 0 CLK 0 */
				/*BP10G_WRITE_REG(pbpctl_dev, I2CCTL, (ctrl_ext&~(BP10G_MCLK_DATA_OUT9|BP10G_MDIO_DATA_OUT9))); */
				BP10G_WRITE_REG(pbpctl_dev, I2CCTL,
						(ctrl_ext &
						 ~BP10G_MDIO_DATA_OUT9));
				BP10G_WRITE_REG(pbpctl_dev_c, ESDP,
						((ctrl | BP10G_MCLK_DIR_OUT9) &
						 ~(BP10G_MCLK_DATA_OUT9)));

			} else if (pbpctl_dev->bp_fiber5) {
				BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL,
						   ((ctrl_ext |
						     BPCTLI_CTRL_EXT_MCLK_DIR5 |
						     BPCTLI_CTRL_EXT_MDIO_DIR5)
						    &
						    ~(BPCTLI_CTRL_EXT_MCLK_DATA5
						      |
						      BPCTLI_CTRL_EXT_MDIO_DATA5)));

			} else if (pbpctl_dev->bp_i80) {
				BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL,
						   ((ctrl_ext |
						     BPCTLI_CTRL_EXT_MDIO_DIR80)
						    &
						    ~BPCTLI_CTRL_EXT_MDIO_DATA80));
				BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT,
						   ((ctrl |
						     BPCTLI_CTRL_EXT_MCLK_DIR80)
						    &
						    ~
						    (BPCTLI_CTRL_EXT_MCLK_DATA80)));

			} else if (pbpctl_dev->bp_540) {
				BP10G_WRITE_REG(pbpctl_dev, ESDP,
						((ctrl | BP540_MCLK_DIR |
						  BP540_MDIO_DIR) &
						 ~(BP540_MDIO_DATA |
						   BP540_MCLK_DATA)));
			} else if (pbpctl_dev->bp_10gb) {

				BP10GB_WRITE_REG(pbpctl_dev, MISC_REG_SPIO,
						 (ctrl_ext | BP10GB_MDIO_CLR |
						  BP10GB_MCLK_CLR) &
						 ~(BP10GB_MCLK_DIR |
						   BP10GB_MDIO_DIR |
						   BP10GB_MDIO_SET |
						   BP10GB_MCLK_SET));

			} else if (!pbpctl_dev->bp_10g)
				BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT,
						   ((ctrl_ext |
						     BPCTLI_CTRL_EXT_MCLK_DIR |
						     BPCTLI_CTRL_EXT_MDIO_DIR) &
						    ~(BPCTLI_CTRL_EXT_MCLK_DATA
						      |
						      BPCTLI_CTRL_EXT_MDIO_DATA)));
			else {

				BP10G_WRITE_REG(pbpctl_dev, EODSDP,
						(ctrl_ext &
						 ~(BP10G_MCLK_DATA_OUT |
						   BP10G_MDIO_DATA_OUT)));
			}

			usec_delay(PULSE_TIME);
		}

	}
}

static int read_pulse(bpctl_dev_t *pbpctl_dev, unsigned int ctrl_ext,
		      unsigned char len)
{
	unsigned char ctrl_val = 0;
	unsigned int i = len;
	unsigned int ctrl = 0;
	bpctl_dev_t *pbpctl_dev_c = NULL;

	if (pbpctl_dev->bp_i80)
		ctrl = BPCTL_READ_REG(pbpctl_dev, CTRL_EXT);
	if (pbpctl_dev->bp_540)
		ctrl = BP10G_READ_REG(pbpctl_dev, ESDP);
	if (pbpctl_dev->bp_10g9) {
		if (!(pbpctl_dev_c = get_status_port_fn(pbpctl_dev)))
			return -1;
		ctrl = BP10G_READ_REG(pbpctl_dev_c, ESDP);
	}


	while (i--) {
		if (pbpctl_dev->bp_10g9) {
			/*BP10G_WRITE_REG(pbpctl_dev, I2CCTL, ((ctrl_ext|BP10G_MDIO_DATA_OUT9)&~BP10G_MCLK_DATA_OUT9)); */
			/* DATA ? CLK 0 */
			BP10G_WRITE_REG(pbpctl_dev_c, ESDP,
					((ctrl | BP10G_MCLK_DIR_OUT9) &
					 ~(BP10G_MCLK_DATA_OUT9)));

		} else if (pbpctl_dev->bp_fiber5) {
			BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL, ((ctrl_ext |
							       BPCTLI_CTRL_EXT_MCLK_DIR5)
							      &
							      ~
							      (BPCTLI_CTRL_EXT_MDIO_DIR5
							       |
							       BPCTLI_CTRL_EXT_MCLK_DATA5)));

		} else if (pbpctl_dev->bp_i80) {
			BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL,
					   (ctrl_ext &
					    ~BPCTLI_CTRL_EXT_MDIO_DIR80));
			BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT,
					   ((ctrl | BPCTLI_CTRL_EXT_MCLK_DIR80)
					    & ~(BPCTLI_CTRL_EXT_MCLK_DATA80)));

		} else if (pbpctl_dev->bp_540) {
			BP10G_WRITE_REG(pbpctl_dev, ESDP,
					((ctrl | BP540_MCLK_DIR) &
					 ~(BP540_MDIO_DIR | BP540_MCLK_DATA)));

		} else if (pbpctl_dev->bp_10gb) {

			BP10GB_WRITE_REG(pbpctl_dev, MISC_REG_SPIO,
					 (ctrl_ext | BP10GB_MDIO_DIR |
					  BP10GB_MCLK_CLR) & ~(BP10GB_MCLK_DIR |
							       BP10GB_MDIO_CLR |
							       BP10GB_MDIO_SET |
							       BP10GB_MCLK_SET));

		} else if (!pbpctl_dev->bp_10g)
			BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ((ctrl_ext |
								   BPCTLI_CTRL_EXT_MCLK_DIR)
								  &
								  ~
								  (BPCTLI_CTRL_EXT_MDIO_DIR
								   |
								   BPCTLI_CTRL_EXT_MCLK_DATA)));
		else {

			BP10G_WRITE_REG(pbpctl_dev, EODSDP, ((ctrl_ext | BP10G_MDIO_DATA_OUT) & ~BP10G_MCLK_DATA_OUT));	/* ? */
			/*    printk("0x28=0x%x\n",BP10G_READ_REG(pbpctl_dev,EODSDP);); */

		}

		usec_delay(PULSE_TIME);
		if (pbpctl_dev->bp_10g9) {
			/*BP10G_WRITE_REG(pbpctl_dev, I2CCTL, (ctrl_ext|BP10G_MCLK_DATA_OUT9|BP10G_MDIO_DATA_OUT9)); */
			/* DATA ? CLK 1 */
			BP10G_WRITE_REG(pbpctl_dev_c, ESDP,
					(ctrl | BP10G_MCLK_DATA_OUT9 |
					 BP10G_MCLK_DIR_OUT9));

		} else if (pbpctl_dev->bp_fiber5) {
			BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL, ((ctrl_ext |
							       BPCTLI_CTRL_EXT_MCLK_DIR5
							       |
							       BPCTLI_CTRL_EXT_MCLK_DATA5)
							      &
							      ~
							      (BPCTLI_CTRL_EXT_MDIO_DIR5)));

		} else if (pbpctl_dev->bp_i80) {
			BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL,
					   (ctrl_ext &
					    ~(BPCTLI_CTRL_EXT_MDIO_DIR80)));
			BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT,
					   (ctrl | BPCTLI_CTRL_EXT_MCLK_DIR80 |
					    BPCTLI_CTRL_EXT_MCLK_DATA80));

		} else if (pbpctl_dev->bp_540) {
			BP10G_WRITE_REG(pbpctl_dev, ESDP,
					((ctrl | BP540_MCLK_DIR |
					  BP540_MCLK_DATA) &
					 ~(BP540_MDIO_DIR)));

		} else if (pbpctl_dev->bp_10gb) {
			BP10GB_WRITE_REG(pbpctl_dev, MISC_REG_SPIO,
					 (ctrl_ext | BP10GB_MDIO_DIR |
					  BP10GB_MCLK_SET) & ~(BP10GB_MCLK_DIR |
							       BP10GB_MDIO_CLR |
							       BP10GB_MDIO_SET |
							       BP10GB_MCLK_CLR));

		} else if (!pbpctl_dev->bp_10g)
			BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ((ctrl_ext |
								   BPCTLI_CTRL_EXT_MCLK_DIR
								   |
								   BPCTLI_CTRL_EXT_MCLK_DATA)
								  &
								  ~
								  (BPCTLI_CTRL_EXT_MDIO_DIR)));
		else {

			BP10G_WRITE_REG(pbpctl_dev, EODSDP,
					(ctrl_ext | BP10G_MCLK_DATA_OUT |
					 BP10G_MDIO_DATA_OUT));

		}
		if (pbpctl_dev->bp_10g9) {
			ctrl_ext = BP10G_READ_REG(pbpctl_dev, I2CCTL);

		} else if ((pbpctl_dev->bp_fiber5) || (pbpctl_dev->bp_i80)) {
			ctrl_ext = BPCTL_READ_REG(pbpctl_dev, CTRL);
		} else if (pbpctl_dev->bp_540) {
			ctrl_ext = BP10G_READ_REG(pbpctl_dev, ESDP);
		} else if (pbpctl_dev->bp_10gb)
			ctrl_ext = BP10GB_READ_REG(pbpctl_dev, MISC_REG_SPIO);

		else if (!pbpctl_dev->bp_10g)
			ctrl_ext = BPCTL_READ_REG(pbpctl_dev, CTRL_EXT);
		else
			ctrl_ext = BP10G_READ_REG(pbpctl_dev, EODSDP);

		usec_delay(PULSE_TIME);
		if (pbpctl_dev->bp_10g9) {
			if (ctrl_ext & BP10G_MDIO_DATA_IN9)
				ctrl_val |= 1 << i;

		} else if (pbpctl_dev->bp_fiber5) {
			if (ctrl_ext & BPCTLI_CTRL_EXT_MDIO_DATA5)
				ctrl_val |= 1 << i;
		} else if (pbpctl_dev->bp_i80) {
			if (ctrl_ext & BPCTLI_CTRL_EXT_MDIO_DATA80)
				ctrl_val |= 1 << i;
		} else if (pbpctl_dev->bp_540) {
			if (ctrl_ext & BP540_MDIO_DATA)
				ctrl_val |= 1 << i;
		} else if (pbpctl_dev->bp_10gb) {
			if (ctrl_ext & BP10GB_MDIO_DATA)
				ctrl_val |= 1 << i;

		} else if (!pbpctl_dev->bp_10g) {

			if (ctrl_ext & BPCTLI_CTRL_EXT_MDIO_DATA)
				ctrl_val |= 1 << i;
		} else {

			if (ctrl_ext & BP10G_MDIO_DATA_IN)
				ctrl_val |= 1 << i;
		}

	}

	return ctrl_val;
}

static void write_reg(bpctl_dev_t *pbpctl_dev, unsigned char value,
		      unsigned char addr)
{
	uint32_t ctrl_ext = 0, ctrl = 0;
	bpctl_dev_t *pbpctl_dev_c = NULL;
	unsigned long flags;
	if (pbpctl_dev->bp_10g9) {
		if (!(pbpctl_dev_c = get_status_port_fn(pbpctl_dev)))
			return;
	}
	if ((pbpctl_dev->wdt_status == WDT_STATUS_EN) &&
	    (pbpctl_dev->bp_ext_ver < PXG4BPFI_VER))
		wdt_time_left(pbpctl_dev);

#ifdef BP_SYNC_FLAG
	spin_lock_irqsave(&pbpctl_dev->bypass_wr_lock, flags);
#else
	atomic_set(&pbpctl_dev->wdt_busy, 1);
#endif
	if (pbpctl_dev->bp_10g9) {

		ctrl_ext = BP10G_READ_REG(pbpctl_dev, I2CCTL);
		ctrl = BP10G_READ_REG(pbpctl_dev_c, ESDP);
		/* DATA 0 CLK 0 */
		/* BP10G_WRITE_REG(pbpctl_dev, I2CCTL, (ctrl_ext&~(BP10G_MCLK_DATA_OUT9|BP10G_MDIO_DATA_OUT9))); */
		BP10G_WRITE_REG(pbpctl_dev, I2CCTL,
				(ctrl_ext & ~BP10G_MDIO_DATA_OUT9));
		BP10G_WRITE_REG(pbpctl_dev_c, ESDP,
				((ctrl | BP10G_MCLK_DIR_OUT9) &
				 ~(BP10G_MCLK_DATA_OUT9)));

	} else if (pbpctl_dev->bp_fiber5) {
		ctrl_ext = BPCTL_READ_REG(pbpctl_dev, CTRL);
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL, ((ctrl_ext |
						       BPCTLI_CTRL_EXT_MCLK_DIR5
						       |
						       BPCTLI_CTRL_EXT_MDIO_DIR5)
						      &
						      ~
						      (BPCTLI_CTRL_EXT_MDIO_DATA5
						       |
						       BPCTLI_CTRL_EXT_MCLK_DATA5)));
	} else if (pbpctl_dev->bp_i80) {
		ctrl_ext = BPCTL_READ_REG(pbpctl_dev, CTRL);
		ctrl = BPCTL_READ_REG(pbpctl_dev, CTRL_EXT);
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL, ((ctrl_ext |
						       BPCTLI_CTRL_EXT_MDIO_DIR80)
						      &
						      ~BPCTLI_CTRL_EXT_MDIO_DATA80));
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT,
				   ((ctrl | BPCTLI_CTRL_EXT_MCLK_DIR80) &
				    ~BPCTLI_CTRL_EXT_MCLK_DATA80));

	} else if (pbpctl_dev->bp_540) {
		ctrl = ctrl_ext = BP10G_READ_REG(pbpctl_dev, ESDP);
		BP10G_WRITE_REG(pbpctl_dev, ESDP, ((ctrl |
						    BP540_MDIO_DIR |
						    BP540_MCLK_DIR) &
						   ~(BP540_MDIO_DATA |
						     BP540_MCLK_DATA)));

	} else if (pbpctl_dev->bp_10gb) {
		ctrl_ext = BP10GB_READ_REG(pbpctl_dev, MISC_REG_SPIO);

		BP10GB_WRITE_REG(pbpctl_dev, MISC_REG_SPIO,
				 (ctrl_ext | BP10GB_MDIO_CLR | BP10GB_MCLK_CLR)
				 & ~(BP10GB_MCLK_DIR | BP10GB_MDIO_DIR |
				     BP10GB_MDIO_SET | BP10GB_MCLK_SET));

	} else if (!pbpctl_dev->bp_10g) {

		ctrl_ext = BPCTL_READ_REG(pbpctl_dev, CTRL_EXT);
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ((ctrl_ext |
							   BPCTLI_CTRL_EXT_MCLK_DIR
							   |
							   BPCTLI_CTRL_EXT_MDIO_DIR)
							  &
							  ~
							  (BPCTLI_CTRL_EXT_MDIO_DATA
							   |
							   BPCTLI_CTRL_EXT_MCLK_DATA)));
	} else {
		ctrl = BP10G_READ_REG(pbpctl_dev, ESDP);
		ctrl_ext = BP10G_READ_REG(pbpctl_dev, EODSDP);
		BP10G_WRITE_REG(pbpctl_dev, EODSDP,
				(ctrl_ext &
				 ~(BP10G_MCLK_DATA_OUT | BP10G_MDIO_DATA_OUT)));
	}
	usec_delay(CMND_INTERVAL);

	/*send sync cmd */
	write_pulse(pbpctl_dev, ctrl_ext, SYNC_CMD_VAL, SYNC_CMD_LEN);
	/*send wr cmd */
	write_pulse(pbpctl_dev, ctrl_ext, WR_CMD_VAL, WR_CMD_LEN);
	write_pulse(pbpctl_dev, ctrl_ext, addr, ADDR_CMD_LEN);

	/*write data */
	write_pulse(pbpctl_dev, ctrl_ext, value, WR_DATA_LEN);
	if (pbpctl_dev->bp_10g9) {
		/*BP10G_WRITE_REG(pbpctl_dev, I2CCTL, (ctrl_ext&~(BP10G_MCLK_DATA_OUT9|BP10G_MDIO_DATA_OUT9))); */
		/* DATA 0 CLK 0 */
		BP10G_WRITE_REG(pbpctl_dev, I2CCTL,
				(ctrl_ext & ~BP10G_MDIO_DATA_OUT9));
		BP10G_WRITE_REG(pbpctl_dev_c, ESDP,
				((ctrl | BP10G_MCLK_DIR_OUT9) &
				 ~(BP10G_MCLK_DATA_OUT9)));

	} else if (pbpctl_dev->bp_fiber5) {
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL, ((ctrl_ext |
						       BPCTLI_CTRL_EXT_MCLK_DIR5
						       |
						       BPCTLI_CTRL_EXT_MDIO_DIR5)
						      &
						      ~
						      (BPCTLI_CTRL_EXT_MDIO_DATA5
						       |
						       BPCTLI_CTRL_EXT_MCLK_DATA5)));
	} else if (pbpctl_dev->bp_i80) {
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL, ((ctrl_ext |
						       BPCTLI_CTRL_EXT_MDIO_DIR80)
						      &
						      ~BPCTLI_CTRL_EXT_MDIO_DATA80));
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT,
				   ((ctrl | BPCTLI_CTRL_EXT_MCLK_DIR80) &
				    ~BPCTLI_CTRL_EXT_MCLK_DATA80));
	} else if (pbpctl_dev->bp_540) {
		BP10G_WRITE_REG(pbpctl_dev, ESDP, ((ctrl |
						    BP540_MDIO_DIR |
						    BP540_MCLK_DIR) &
						   ~(BP540_MDIO_DATA |
						     BP540_MCLK_DATA)));
	} else if (pbpctl_dev->bp_10gb) {
		BP10GB_WRITE_REG(pbpctl_dev, MISC_REG_SPIO,
				 (ctrl_ext | BP10GB_MDIO_CLR | BP10GB_MCLK_CLR)
				 & ~(BP10GB_MCLK_DIR | BP10GB_MDIO_DIR |
				     BP10GB_MDIO_SET | BP10GB_MCLK_SET));

	} else if (!pbpctl_dev->bp_10g)

		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ((ctrl_ext |
							   BPCTLI_CTRL_EXT_MCLK_DIR
							   |
							   BPCTLI_CTRL_EXT_MDIO_DIR)
							  &
							  ~
							  (BPCTLI_CTRL_EXT_MDIO_DATA
							   |
							   BPCTLI_CTRL_EXT_MCLK_DATA)));
	else {
		BP10G_WRITE_REG(pbpctl_dev, EODSDP,
				(ctrl_ext &
				 ~(BP10G_MCLK_DATA_OUT | BP10G_MDIO_DATA_OUT)));

	}

	usec_delay(CMND_INTERVAL * 4);

	if ((pbpctl_dev->wdt_status == WDT_STATUS_EN) &&
	    (pbpctl_dev->bp_ext_ver < PXG4BPFI_VER) && (addr == CMND_REG_ADDR))
		pbpctl_dev->bypass_wdt_on_time = jiffies;
#ifdef BP_SYNC_FLAG
	spin_unlock_irqrestore(&pbpctl_dev->bypass_wr_lock, flags);
#else
	atomic_set(&pbpctl_dev->wdt_busy, 0);
#endif

}

static void write_data(bpctl_dev_t *pbpctl_dev, unsigned char value)
{
	write_reg(pbpctl_dev, value, CMND_REG_ADDR);
}

static int read_reg(bpctl_dev_t *pbpctl_dev, unsigned char addr)
{
	uint32_t ctrl_ext = 0, ctrl = 0, ctrl_value = 0;
	bpctl_dev_t *pbpctl_dev_c = NULL;

#ifdef BP_SYNC_FLAG
	unsigned long flags;
	spin_lock_irqsave(&pbpctl_dev->bypass_wr_lock, flags);
#else
	atomic_set(&pbpctl_dev->wdt_busy, 1);
#endif
	if (pbpctl_dev->bp_10g9) {
		if (!(pbpctl_dev_c = get_status_port_fn(pbpctl_dev)))
			return -1;
	}

	if (pbpctl_dev->bp_10g9) {
		ctrl_ext = BP10G_READ_REG(pbpctl_dev, I2CCTL);
		ctrl = BP10G_READ_REG(pbpctl_dev_c, ESDP);

		/* BP10G_WRITE_REG(pbpctl_dev, I2CCTL, (ctrl_ext&~(BP10G_MCLK_DATA_OUT9|BP10G_MDIO_DATA_OUT9))); */
		/* DATA 0 CLK 0 */
		BP10G_WRITE_REG(pbpctl_dev, I2CCTL,
				(ctrl_ext & ~BP10G_MDIO_DATA_OUT9));
		BP10G_WRITE_REG(pbpctl_dev_c, ESDP,
				((ctrl | BP10G_MCLK_DIR_OUT9) &
				 ~(BP10G_MCLK_DATA_OUT9)));

	} else if (pbpctl_dev->bp_fiber5) {
		ctrl_ext = BPCTL_READ_REG(pbpctl_dev, CTRL);

		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL, ((ctrl_ext |
						       BPCTLI_CTRL_EXT_MCLK_DIR5
						       |
						       BPCTLI_CTRL_EXT_MDIO_DIR5)
						      &
						      ~
						      (BPCTLI_CTRL_EXT_MDIO_DATA5
						       |
						       BPCTLI_CTRL_EXT_MCLK_DATA5)));
	} else if (pbpctl_dev->bp_i80) {
		ctrl_ext = BPCTL_READ_REG(pbpctl_dev, CTRL);
		ctrl = BPCTL_READ_REG(pbpctl_dev, CTRL_EXT);

		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL, ((ctrl_ext |
						       BPCTLI_CTRL_EXT_MDIO_DIR80)
						      &
						      ~BPCTLI_CTRL_EXT_MDIO_DATA80));
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT,
				   ((ctrl | BPCTLI_CTRL_EXT_MCLK_DIR80) &
				    ~BPCTLI_CTRL_EXT_MCLK_DATA80));
	} else if (pbpctl_dev->bp_540) {
		ctrl_ext = BP10G_READ_REG(pbpctl_dev, ESDP);
		ctrl = BP10G_READ_REG(pbpctl_dev, ESDP);

		BP10G_WRITE_REG(pbpctl_dev, ESDP, ((ctrl | BP540_MCLK_DIR |
						    BP540_MDIO_DIR) &
						   ~(BP540_MDIO_DATA |
						     BP540_MCLK_DATA)));
	} else if (pbpctl_dev->bp_10gb) {
		ctrl_ext = BP10GB_READ_REG(pbpctl_dev, MISC_REG_SPIO);

		BP10GB_WRITE_REG(pbpctl_dev, MISC_REG_SPIO,
				 (ctrl_ext | BP10GB_MDIO_CLR | BP10GB_MCLK_CLR)
				 & ~(BP10GB_MCLK_DIR | BP10GB_MDIO_DIR |
				     BP10GB_MDIO_SET | BP10GB_MCLK_SET));
#if 0

		/*BP10GB_WRITE_REG(pbpctl_dev, MISC_REG_SPIO, (ctrl_ext | BP10GB_MCLK_DIR | BP10GB_MDIO_DIR|
		   BP10GB_MCLK_CLR|BP10GB_MDIO_CLR));
		   ctrl_ext = BP10GB_READ_REG(pbpctl_dev, MISC_REG_SPIO);
		   printk("1reg=%x\n", ctrl_ext); */

		BP10GB_WRITE_REG(pbpctl_dev, MISC_REG_SPIO, ((ctrl_ext |
							      BP10GB_MCLK_SET |
							      BP10GB_MDIO_CLR))
				 & ~(BP10GB_MCLK_CLR | BP10GB_MDIO_SET |
				     BP10GB_MCLK_DIR | BP10GB_MDIO_DIR));

		/*   bnx2x_set_spio(pbpctl_dev, 5, MISC_REGISTERS_SPIO_OUTPUT_LOW);
		   bnx2x_set_spio(pbpctl_dev, 4, MISC_REGISTERS_SPIO_OUTPUT_LOW);
		   bnx2x_set_spio(pbpctl_dev, 4, MISC_REGISTERS_SPIO_INPUT_HI_Z); */

		ctrl_ext = BP10GB_READ_REG(pbpctl_dev, MISC_REG_SPIO);

		printk("2reg=%x\n", ctrl_ext);

#ifdef BP_SYNC_FLAG
		spin_unlock_irqrestore(&pbpctl_dev->bypass_wr_lock, flags);
#else
		atomic_set(&pbpctl_dev->wdt_busy, 0);
#endif

		return 0;

#endif

	} else if (!pbpctl_dev->bp_10g) {

		ctrl_ext = BPCTL_READ_REG(pbpctl_dev, CTRL_EXT);

		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ((ctrl_ext |
							   BPCTLI_CTRL_EXT_MCLK_DIR
							   |
							   BPCTLI_CTRL_EXT_MDIO_DIR)
							  &
							  ~
							  (BPCTLI_CTRL_EXT_MDIO_DATA
							   |
							   BPCTLI_CTRL_EXT_MCLK_DATA)));
	} else {

		ctrl = BP10G_READ_REG(pbpctl_dev, ESDP);
		ctrl_ext = BP10G_READ_REG(pbpctl_dev, EODSDP);
		BP10G_WRITE_REG(pbpctl_dev, EODSDP,
				(ctrl_ext &
				 ~(BP10G_MCLK_DATA_OUT | BP10G_MDIO_DATA_OUT)));

	}

	usec_delay(CMND_INTERVAL);

	/*send sync cmd */
	write_pulse(pbpctl_dev, ctrl_ext, SYNC_CMD_VAL, SYNC_CMD_LEN);
	/*send rd cmd */
	write_pulse(pbpctl_dev, ctrl_ext, RD_CMD_VAL, RD_CMD_LEN);
	/*send addr */
	write_pulse(pbpctl_dev, ctrl_ext, addr, ADDR_CMD_LEN);
	/*read data */
	/* zero */
	if (pbpctl_dev->bp_10g9) {
		/* DATA 0 CLK 1 */
		/*BP10G_WRITE_REG(pbpctl_dev, I2CCTL, (ctrl_ext|BP10G_MCLK_DATA_OUT9|BP10G_MDIO_DATA_OUT9)); */
		BP10G_WRITE_REG(pbpctl_dev, I2CCTL,
				(ctrl_ext | BP10G_MDIO_DATA_OUT9));
		BP10G_WRITE_REG(pbpctl_dev_c, ESDP,
				(ctrl | BP10G_MCLK_DATA_OUT9 |
				 BP10G_MCLK_DIR_OUT9));

	} else if (pbpctl_dev->bp_fiber5) {
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL, ((ctrl_ext |
						       BPCTLI_CTRL_EXT_MCLK_DIR5
						       |
						       BPCTLI_CTRL_EXT_MCLK_DATA5)
						      &
						      ~
						      (BPCTLI_CTRL_EXT_MDIO_DIR5
						       |
						       BPCTLI_CTRL_EXT_MDIO_DATA5)));

	} else if (pbpctl_dev->bp_i80) {
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL,
				   (ctrl_ext &
				    ~(BPCTLI_CTRL_EXT_MDIO_DATA80 |
				      BPCTLI_CTRL_EXT_MDIO_DIR80)));
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT,
				   (ctrl | BPCTLI_CTRL_EXT_MCLK_DIR80 |
				    BPCTLI_CTRL_EXT_MCLK_DATA80));

	} else if (pbpctl_dev->bp_540) {
		BP10G_WRITE_REG(pbpctl_dev, ESDP,
				(((ctrl | BP540_MDIO_DIR | BP540_MCLK_DIR |
				   BP540_MCLK_DATA) & ~BP540_MDIO_DATA)));

	} else if (pbpctl_dev->bp_10gb) {

		BP10GB_WRITE_REG(pbpctl_dev, MISC_REG_SPIO,
				 (ctrl_ext | BP10GB_MDIO_DIR | BP10GB_MCLK_SET)
				 & ~(BP10GB_MCLK_DIR | BP10GB_MDIO_SET |
				     BP10GB_MDIO_CLR | BP10GB_MCLK_CLR));

	} else if (!pbpctl_dev->bp_10g)
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ((ctrl_ext |
							   BPCTLI_CTRL_EXT_MCLK_DIR
							   |
							   BPCTLI_CTRL_EXT_MCLK_DATA)
							  &
							  ~
							  (BPCTLI_CTRL_EXT_MDIO_DIR
							   |
							   BPCTLI_CTRL_EXT_MDIO_DATA)));
	else {

		BP10G_WRITE_REG(pbpctl_dev, EODSDP,
				(ctrl_ext | BP10G_MCLK_DATA_OUT |
				 BP10G_MDIO_DATA_OUT));


	}
	usec_delay(PULSE_TIME);

	ctrl_value = read_pulse(pbpctl_dev, ctrl_ext, RD_DATA_LEN);

	if (pbpctl_dev->bp_10g9) {
		ctrl_ext = BP10G_READ_REG(pbpctl_dev, I2CCTL);
		ctrl = BP10G_READ_REG(pbpctl_dev_c, ESDP);

		/* BP10G_WRITE_REG(pbpctl_dev, I2CCTL, (ctrl_ext&~(BP10G_MCLK_DATA_OUT9|BP10G_MDIO_DATA_OUT9))); */
		/* DATA 0 CLK 0 */
		BP10G_WRITE_REG(pbpctl_dev, I2CCTL,
				(ctrl_ext & ~BP10G_MDIO_DATA_OUT9));
		BP10G_WRITE_REG(pbpctl_dev_c, ESDP,
				((ctrl | BP10G_MCLK_DIR_OUT9) &
				 ~(BP10G_MCLK_DATA_OUT9)));

	} else if (pbpctl_dev->bp_fiber5) {
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL, ((ctrl_ext |
						       BPCTLI_CTRL_EXT_MCLK_DIR5
						       |
						       BPCTLI_CTRL_EXT_MDIO_DIR5)
						      &
						      ~
						      (BPCTLI_CTRL_EXT_MDIO_DATA5
						       |
						       BPCTLI_CTRL_EXT_MCLK_DATA5)));
	} else if (pbpctl_dev->bp_i80) {
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL, ((ctrl_ext |
						       BPCTLI_CTRL_EXT_MDIO_DIR80)
						      &
						      ~BPCTLI_CTRL_EXT_MDIO_DATA80));
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT,
				   ((ctrl | BPCTLI_CTRL_EXT_MCLK_DIR80) &
				    ~BPCTLI_CTRL_EXT_MCLK_DATA80));

	} else if (pbpctl_dev->bp_540) {
		ctrl = BP10G_READ_REG(pbpctl_dev, ESDP);
		BP10G_WRITE_REG(pbpctl_dev, ESDP, ((ctrl | BP540_MCLK_DIR |
						    BP540_MDIO_DIR) &
						   ~(BP540_MDIO_DATA |
						     BP540_MCLK_DATA)));

	} else if (pbpctl_dev->bp_10gb) {
		ctrl_ext = BP10GB_READ_REG(pbpctl_dev, MISC_REG_SPIO);
		BP10GB_WRITE_REG(pbpctl_dev, MISC_REG_SPIO,
				 (ctrl_ext | BP10GB_MDIO_CLR | BP10GB_MCLK_CLR)
				 & ~(BP10GB_MCLK_DIR | BP10GB_MDIO_DIR |
				     BP10GB_MDIO_SET | BP10GB_MCLK_SET));

	} else if (!pbpctl_dev->bp_10g) {
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ((ctrl_ext |
							   BPCTLI_CTRL_EXT_MCLK_DIR
							   |
							   BPCTLI_CTRL_EXT_MDIO_DIR)
							  &
							  ~
							  (BPCTLI_CTRL_EXT_MDIO_DATA
							   |
							   BPCTLI_CTRL_EXT_MCLK_DATA)));
	} else {

		ctrl = BP10G_READ_REG(pbpctl_dev, ESDP);
		ctrl_ext = BP10G_READ_REG(pbpctl_dev, EODSDP);
		BP10G_WRITE_REG(pbpctl_dev, EODSDP,
				(ctrl_ext &
				 ~(BP10G_MCLK_DATA_OUT | BP10G_MDIO_DATA_OUT)));

	}

	usec_delay(CMND_INTERVAL * 4);
#ifdef BP_SYNC_FLAG
	spin_unlock_irqrestore(&pbpctl_dev->bypass_wr_lock, flags);
#else
	atomic_set(&pbpctl_dev->wdt_busy, 0);
#endif

	return ctrl_value;
}

static int wdt_pulse(bpctl_dev_t *pbpctl_dev)
{
	uint32_t ctrl_ext = 0, ctrl = 0;
	bpctl_dev_t *pbpctl_dev_c = NULL;

#ifdef BP_SYNC_FLAG
	unsigned long flags;

	spin_lock_irqsave(&pbpctl_dev->bypass_wr_lock, flags);
#else

	if ((atomic_read(&pbpctl_dev->wdt_busy)) == 1)
		return -1;
#endif
	if (pbpctl_dev->bp_10g9) {
		if (!(pbpctl_dev_c = get_status_port_fn(pbpctl_dev)))
			return -1;
	}

	if (pbpctl_dev->bp_10g9) {
		ctrl_ext = BP10G_READ_REG(pbpctl_dev, I2CCTL);
		ctrl = BP10G_READ_REG(pbpctl_dev_c, ESDP);

		/* BP10G_WRITE_REG(pbpctl_dev, I2CCTL, (ctrl_ext&~(BP10G_MCLK_DATA_OUT9|BP10G_MDIO_DATA_OUT9))); */
		/* DATA 0 CLK 0 */
		BP10G_WRITE_REG(pbpctl_dev, I2CCTL,
				(ctrl_ext & ~BP10G_MDIO_DATA_OUT9));
		BP10G_WRITE_REG(pbpctl_dev_c, ESDP,
				((ctrl | BP10G_MCLK_DIR_OUT9) &
				 ~(BP10G_MCLK_DATA_OUT9)));

	} else if (pbpctl_dev->bp_fiber5) {
		ctrl_ext = BPCTL_READ_REG(pbpctl_dev, CTRL);
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL, ((ctrl_ext |
						       BPCTLI_CTRL_EXT_MCLK_DIR5
						       |
						       BPCTLI_CTRL_EXT_MDIO_DIR5)
						      &
						      ~
						      (BPCTLI_CTRL_EXT_MDIO_DATA5
						       |
						       BPCTLI_CTRL_EXT_MCLK_DATA5)));
	} else if (pbpctl_dev->bp_i80) {
		ctrl_ext = BPCTL_READ_REG(pbpctl_dev, CTRL);
		ctrl = BPCTL_READ_REG(pbpctl_dev, CTRL_EXT);
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL, ((ctrl_ext |
						       BPCTLI_CTRL_EXT_MDIO_DIR80)
						      &
						      ~BPCTLI_CTRL_EXT_MDIO_DATA80));
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT,
				   ((ctrl | BPCTLI_CTRL_EXT_MCLK_DIR80) &
				    ~BPCTLI_CTRL_EXT_MCLK_DATA80));
	} else if (pbpctl_dev->bp_540) {
		ctrl_ext = ctrl = BP10G_READ_REG(pbpctl_dev, ESDP);
		BP10G_WRITE_REG(pbpctl_dev, ESDP, ((ctrl | BP540_MCLK_DIR |
						    BP540_MDIO_DIR) &
						   ~(BP540_MDIO_DATA |
						     BP540_MCLK_DATA)));
	} else if (pbpctl_dev->bp_10gb) {
		ctrl_ext = BP10GB_READ_REG(pbpctl_dev, MISC_REG_SPIO);
		BP10GB_WRITE_REG(pbpctl_dev, MISC_REG_SPIO,
				 (ctrl_ext | BP10GB_MDIO_CLR | BP10GB_MCLK_CLR)
				 & ~(BP10GB_MCLK_DIR | BP10GB_MDIO_DIR |
				     BP10GB_MDIO_SET | BP10GB_MCLK_SET));

	} else if (!pbpctl_dev->bp_10g) {

		ctrl_ext = BPCTL_READ_REG(pbpctl_dev, CTRL_EXT);
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ((ctrl_ext |
							   BPCTLI_CTRL_EXT_MCLK_DIR
							   |
							   BPCTLI_CTRL_EXT_MDIO_DIR)
							  &
							  ~
							  (BPCTLI_CTRL_EXT_MDIO_DATA
							   |
							   BPCTLI_CTRL_EXT_MCLK_DATA)));
	} else {

		ctrl = BP10G_READ_REG(pbpctl_dev, ESDP);
		ctrl_ext = BP10G_READ_REG(pbpctl_dev, EODSDP);
		BP10G_WRITE_REG(pbpctl_dev, EODSDP,
				(ctrl_ext &
				 ~(BP10G_MCLK_DATA_OUT | BP10G_MDIO_DATA_OUT)));

	}
	if (pbpctl_dev->bp_10g9) {
		/*   BP10G_WRITE_REG(pbpctl_dev, I2CCTL, ((ctrl_ext|BP10G_MCLK_DATA_OUT9)&~BP10G_MDIO_DATA_OUT9)); */
		/* DATA 0 CLK 1 */
		BP10G_WRITE_REG(pbpctl_dev, I2CCTL,
				(ctrl_ext & ~BP10G_MDIO_DATA_OUT9));
		BP10G_WRITE_REG(pbpctl_dev_c, ESDP,
				(ctrl | BP10G_MCLK_DATA_OUT9 |
				 BP10G_MCLK_DIR_OUT9));

	} else if (pbpctl_dev->bp_fiber5) {
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL, ((ctrl_ext |
						       BPCTLI_CTRL_EXT_MCLK_DIR5
						       |
						       BPCTLI_CTRL_EXT_MDIO_DIR5
						       |
						       BPCTLI_CTRL_EXT_MCLK_DATA5)
						      &
						      ~
						      (BPCTLI_CTRL_EXT_MDIO_DATA5)));
	} else if (pbpctl_dev->bp_i80) {
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL, ((ctrl_ext |
						       BPCTLI_CTRL_EXT_MDIO_DIR80)
						      &
						      ~BPCTLI_CTRL_EXT_MDIO_DATA80));
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT,
				   (ctrl | BPCTLI_CTRL_EXT_MCLK_DIR80 |
				    BPCTLI_CTRL_EXT_MCLK_DATA80));

	} else if (pbpctl_dev->bp_540) {
		BP10G_WRITE_REG(pbpctl_dev, ESDP, ((ctrl |
						    BP540_MDIO_DIR |
						    BP540_MCLK_DIR |
						    BP540_MCLK_DATA) &
						   ~BP540_MDIO_DATA));

	} else if (pbpctl_dev->bp_10gb) {
		ctrl_ext = BP10GB_READ_REG(pbpctl_dev, MISC_REG_SPIO);

		BP10GB_WRITE_REG(pbpctl_dev, MISC_REG_SPIO,
				 (ctrl_ext | BP10GB_MDIO_CLR | BP10GB_MCLK_SET)
				 & ~(BP10GB_MCLK_DIR | BP10GB_MDIO_DIR |
				     BP10GB_MDIO_SET | BP10GB_MCLK_CLR));

	} else if (!pbpctl_dev->bp_10g)
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ((ctrl_ext |
							   BPCTLI_CTRL_EXT_MCLK_DIR
							   |
							   BPCTLI_CTRL_EXT_MDIO_DIR
							   |
							   BPCTLI_CTRL_EXT_MCLK_DATA)
							  &
							  ~
							  (BPCTLI_CTRL_EXT_MDIO_DATA)));
	else {

		BP10G_WRITE_REG(pbpctl_dev, EODSDP,
				((ctrl_ext | BP10G_MCLK_DATA_OUT) &
				 ~BP10G_MDIO_DATA_OUT));

	}

	usec_delay(WDT_INTERVAL);
	if (pbpctl_dev->bp_10g9) {
		/* BP10G_WRITE_REG(pbpctl_dev, I2CCTL, (ctrl_ext&~(BP10G_MCLK_DATA_OUT9|BP10G_MDIO_DATA_OUT9))); */
		/* DATA 0 CLK 0 */
		BP10G_WRITE_REG(pbpctl_dev, I2CCTL,
				(ctrl_ext & ~BP10G_MDIO_DATA_OUT9));
		BP10G_WRITE_REG(pbpctl_dev_c, ESDP,
				((ctrl | BP10G_MCLK_DIR_OUT9) &
				 ~(BP10G_MCLK_DATA_OUT9)));

	} else if (pbpctl_dev->bp_fiber5) {
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL, ((ctrl_ext |
						       BPCTLI_CTRL_EXT_MCLK_DIR5
						       |
						       BPCTLI_CTRL_EXT_MDIO_DIR5)
						      &
						      ~
						      (BPCTLI_CTRL_EXT_MCLK_DATA5
						       |
						       BPCTLI_CTRL_EXT_MDIO_DATA5)));
	} else if (pbpctl_dev->bp_i80) {
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL, ((ctrl_ext |
						       BPCTLI_CTRL_EXT_MDIO_DIR80)
						      &
						      ~BPCTLI_CTRL_EXT_MDIO_DATA80));
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT,
				   ((ctrl | BPCTLI_CTRL_EXT_MCLK_DIR80) &
				    ~BPCTLI_CTRL_EXT_MCLK_DATA80));

	} else if (pbpctl_dev->bp_540) {
		BP10G_WRITE_REG(pbpctl_dev, ESDP, ((ctrl | BP540_MCLK_DIR |
						    BP540_MDIO_DIR) &
						   ~(BP540_MDIO_DATA |
						     BP540_MCLK_DATA)));

	} else if (pbpctl_dev->bp_10gb) {
		ctrl_ext = BP10GB_READ_REG(pbpctl_dev, MISC_REG_SPIO);
		BP10GB_WRITE_REG(pbpctl_dev, MISC_REG_SPIO,
				 (ctrl_ext | BP10GB_MDIO_CLR | BP10GB_MCLK_CLR)
				 & ~(BP10GB_MCLK_DIR | BP10GB_MDIO_DIR |
				     BP10GB_MDIO_SET | BP10GB_MCLK_SET));

	} else if (!pbpctl_dev->bp_10g)
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ((ctrl_ext |
							   BPCTLI_CTRL_EXT_MCLK_DIR
							   |
							   BPCTLI_CTRL_EXT_MDIO_DIR)
							  &
							  ~
							  (BPCTLI_CTRL_EXT_MCLK_DATA
							   |
							   BPCTLI_CTRL_EXT_MDIO_DATA)));
	else {

		BP10G_WRITE_REG(pbpctl_dev, EODSDP,
				(ctrl_ext &
				 ~(BP10G_MCLK_DATA_OUT | BP10G_MDIO_DATA_OUT)));
	}
	if ((pbpctl_dev->wdt_status == WDT_STATUS_EN)	/*&&
							   (pbpctl_dev->bp_ext_ver<PXG4BPFI_VER) */ )
		pbpctl_dev->bypass_wdt_on_time = jiffies;
#ifdef BP_SYNC_FLAG
	spin_unlock_irqrestore(&pbpctl_dev->bypass_wr_lock, flags);
#endif
	usec_delay(CMND_INTERVAL * 4);
	return 0;
}

static void data_pulse(bpctl_dev_t *pbpctl_dev, unsigned char value)
{

	uint32_t ctrl_ext = 0;
#ifdef BP_SYNC_FLAG
	unsigned long flags;
#endif
	wdt_time_left(pbpctl_dev);
#ifdef BP_SYNC_FLAG
	spin_lock_irqsave(&pbpctl_dev->bypass_wr_lock, flags);
#else
	atomic_set(&pbpctl_dev->wdt_busy, 1);
#endif

	ctrl_ext = BPCTL_READ_REG(pbpctl_dev, CTRL_EXT);
	BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ((ctrl_ext |
						   BPCTLI_CTRL_EXT_SDP6_DIR |
						   BPCTLI_CTRL_EXT_SDP7_DIR) &
						  ~(BPCTLI_CTRL_EXT_SDP6_DATA |
						    BPCTLI_CTRL_EXT_SDP7_DATA)));

	usec_delay(INIT_CMND_INTERVAL);
	BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ((ctrl_ext |
						   BPCTLI_CTRL_EXT_SDP6_DIR |
						   BPCTLI_CTRL_EXT_SDP7_DIR |
						   BPCTLI_CTRL_EXT_SDP6_DATA) &
						  ~
						  (BPCTLI_CTRL_EXT_SDP7_DATA)));
	usec_delay(INIT_CMND_INTERVAL);

	while (value) {
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ctrl_ext |
				   BPCTLI_CTRL_EXT_SDP6_DIR |
				   BPCTLI_CTRL_EXT_SDP7_DIR |
				   BPCTLI_CTRL_EXT_SDP6_DATA |
				   BPCTLI_CTRL_EXT_SDP7_DATA);
		usec_delay(PULSE_INTERVAL);
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ((ctrl_ext |
							   BPCTLI_CTRL_EXT_SDP6_DIR
							   |
							   BPCTLI_CTRL_EXT_SDP7_DIR
							   |
							   BPCTLI_CTRL_EXT_SDP6_DATA)
							  &
							  ~BPCTLI_CTRL_EXT_SDP7_DATA));
		usec_delay(PULSE_INTERVAL);
		value--;

	}
	usec_delay(INIT_CMND_INTERVAL - PULSE_INTERVAL);
	BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ((ctrl_ext |
						   BPCTLI_CTRL_EXT_SDP6_DIR |
						   BPCTLI_CTRL_EXT_SDP7_DIR) &
						  ~(BPCTLI_CTRL_EXT_SDP6_DATA |
						    BPCTLI_CTRL_EXT_SDP7_DATA)));
	usec_delay(WDT_TIME_CNT);
	if (pbpctl_dev->wdt_status == WDT_STATUS_EN)
		pbpctl_dev->bypass_wdt_on_time = jiffies;
#ifdef BP_SYNC_FLAG
	spin_unlock_irqrestore(&pbpctl_dev->bypass_wr_lock, flags);
#else
	atomic_set(&pbpctl_dev->wdt_busy, 0);
#endif

}

static int send_wdt_pulse(bpctl_dev_t *pbpctl_dev)
{
	uint32_t ctrl_ext = 0;

#ifdef BP_SYNC_FLAG
	unsigned long flags;

	spin_lock_irqsave(&pbpctl_dev->bypass_wr_lock, flags);
#else

	if ((atomic_read(&pbpctl_dev->wdt_busy)) == 1)
		return -1;
#endif
	wdt_time_left(pbpctl_dev);
	ctrl_ext = BPCTL_READ_REG(pbpctl_dev, CTRL_EXT);

	BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ctrl_ext |	/* 1 */
			   BPCTLI_CTRL_EXT_SDP7_DIR |
			   BPCTLI_CTRL_EXT_SDP7_DATA);
	usec_delay(PULSE_INTERVAL);
	BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ((ctrl_ext |	/* 0 */
						   BPCTLI_CTRL_EXT_SDP7_DIR) &
						  ~BPCTLI_CTRL_EXT_SDP7_DATA));

	usec_delay(PULSE_INTERVAL);
	if (pbpctl_dev->wdt_status == WDT_STATUS_EN)
		pbpctl_dev->bypass_wdt_on_time = jiffies;
#ifdef BP_SYNC_FLAG
	spin_unlock_irqrestore(&pbpctl_dev->bypass_wr_lock, flags);
#endif

	return 0;
}

void send_bypass_clear_pulse(bpctl_dev_t *pbpctl_dev, unsigned int value)
{
	uint32_t ctrl_ext = 0;

	ctrl_ext = BPCTL_READ_REG(pbpctl_dev, CTRL_EXT);
	BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ((ctrl_ext |	/* 0 */
						   BPCTLI_CTRL_EXT_SDP6_DIR) &
						  ~BPCTLI_CTRL_EXT_SDP6_DATA));

	usec_delay(PULSE_INTERVAL);
	while (value) {
		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ctrl_ext |	/* 1 */
				   BPCTLI_CTRL_EXT_SDP6_DIR |
				   BPCTLI_CTRL_EXT_SDP6_DATA);
		usec_delay(PULSE_INTERVAL);
		value--;
	}
	BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ((ctrl_ext |	/* 0 */
						   BPCTLI_CTRL_EXT_SDP6_DIR) &
						  ~BPCTLI_CTRL_EXT_SDP6_DATA));
	usec_delay(PULSE_INTERVAL);
}

/*  #endif  OLD_FW */
#ifdef BYPASS_DEBUG

int pulse_set_fn(bpctl_dev_t *pbpctl_dev, unsigned int counter)
{
	uint32_t ctrl_ext = 0;

	if (!pbpctl_dev)
		return -1;

	ctrl_ext = BPCTL_READ_REG(pbpctl_dev, CTRL_EXT);
	write_pulse_1(pbpctl_dev, ctrl_ext, counter, counter);

	pbpctl_dev->bypass_wdt_status = 0;
	if (pbpctl_dev->bp_ext_ver >= PXG2BPI_VER) {
		write_pulse_1(pbpctl_dev, ctrl_ext, counter, counter);
	} else {
		wdt_time_left(pbpctl_dev);
		if (pbpctl_dev->wdt_status == WDT_STATUS_EN) {
			pbpctl_dev->wdt_status = 0;
			data_pulse(pbpctl_dev, counter);
			pbpctl_dev->wdt_status = WDT_STATUS_EN;
			pbpctl_dev->bypass_wdt_on_time = jiffies;

		} else
			data_pulse(pbpctl_dev, counter);
	}

	return 0;
}

int zero_set_fn(bpctl_dev_t *pbpctl_dev)
{
	uint32_t ctrl_ext = 0, ctrl_value = 0;
	if (!pbpctl_dev)
		return -1;

	if (pbpctl_dev->bp_ext_ver >= PXG2BPI_VER) {
		printk("zero_set");

		ctrl_ext = BPCTL_READ_REG(pbpctl_dev, CTRL_EXT);

		BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ((ctrl_ext |
							   BPCTLI_CTRL_EXT_MCLK_DIR)
							  &
							  ~
							  (BPCTLI_CTRL_EXT_MCLK_DATA
							   |
							   BPCTLI_CTRL_EXT_MDIO_DIR
							   |
							   BPCTLI_CTRL_EXT_MDIO_DATA)));

	}
	return ctrl_value;
}

int pulse_get2_fn(bpctl_dev_t *pbpctl_dev)
{
	uint32_t ctrl_ext = 0, ctrl_value = 0;
	if (!pbpctl_dev)
		return -1;

	if (pbpctl_dev->bp_ext_ver >= PXG2BPI_VER) {
		printk("pulse_get_fn\n");
		ctrl_ext = BPCTL_READ_REG(pbpctl_dev, CTRL_EXT);
		ctrl_value = read_pulse_2(pbpctl_dev, ctrl_ext);
		printk("read:%d\n", ctrl_value);
	}
	return ctrl_value;
}

int pulse_get1_fn(bpctl_dev_t *pbpctl_dev)
{
	uint32_t ctrl_ext = 0, ctrl_value = 0;
	if (!pbpctl_dev)
		return -1;

	if (pbpctl_dev->bp_ext_ver >= PXG2BPI_VER) {

		printk("pulse_get_fn\n");

		ctrl_ext = BPCTL_READ_REG(pbpctl_dev, CTRL_EXT);
		ctrl_value = read_pulse_1(pbpctl_dev, ctrl_ext);
		printk("read:%d\n", ctrl_value);
	}
	return ctrl_value;
}

int gpio6_set_fn(bpctl_dev_t *pbpctl_dev)
{
	uint32_t ctrl_ext = 0;

	ctrl_ext = BPCTL_READ_REG(pbpctl_dev, CTRL_EXT);
	BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ctrl_ext |
			   BPCTLI_CTRL_EXT_SDP6_DIR |
			   BPCTLI_CTRL_EXT_SDP6_DATA);
	return 0;
}

int gpio7_set_fn(bpctl_dev_t *pbpctl_dev)
{
	uint32_t ctrl_ext = 0;

	ctrl_ext = BPCTL_READ_REG(pbpctl_dev, CTRL_EXT);
	BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ctrl_ext |
			   BPCTLI_CTRL_EXT_SDP7_DIR |
			   BPCTLI_CTRL_EXT_SDP7_DATA);
	return 0;
}

int gpio7_clear_fn(bpctl_dev_t *pbpctl_dev)
{
	uint32_t ctrl_ext = 0;

	ctrl_ext = BPCTL_READ_REG(pbpctl_dev, CTRL_EXT);
	BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ((ctrl_ext |
						   BPCTLI_CTRL_EXT_SDP7_DIR) &
						  ~BPCTLI_CTRL_EXT_SDP7_DATA));
	return 0;
}

int gpio6_clear_fn(bpctl_dev_t *pbpctl_dev)
{
	uint32_t ctrl_ext = 0;

	ctrl_ext = BPCTL_READ_REG(pbpctl_dev, CTRL_EXT);
	BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, ((ctrl_ext |
						   BPCTLI_CTRL_EXT_SDP6_DIR) &
						  ~BPCTLI_CTRL_EXT_SDP6_DATA));
	return 0;
}
#endif				/*BYPASS_DEBUG */

static bpctl_dev_t *get_status_port_fn(bpctl_dev_t *pbpctl_dev)
{
	int idx_dev = 0;

	if (pbpctl_dev == NULL)
		return NULL;

	if ((pbpctl_dev->func == 0) || (pbpctl_dev->func == 2)) {
		for (idx_dev = 0;
		     ((bpctl_dev_arr[idx_dev].pdev != NULL)
		      && (idx_dev < device_num)); idx_dev++) {
			if ((bpctl_dev_arr[idx_dev].bus == pbpctl_dev->bus)
			    && (bpctl_dev_arr[idx_dev].slot == pbpctl_dev->slot)
			    && ((bpctl_dev_arr[idx_dev].func == 1)
				&& (pbpctl_dev->func == 0))) {

				return &(bpctl_dev_arr[idx_dev]);
			}
			if ((bpctl_dev_arr[idx_dev].bus == pbpctl_dev->bus) &&
			    (bpctl_dev_arr[idx_dev].slot == pbpctl_dev->slot) &&
			    ((bpctl_dev_arr[idx_dev].func == 3)
			     && (pbpctl_dev->func == 2))) {

				return &(bpctl_dev_arr[idx_dev]);
			}
		}
	}
	return NULL;
}

static bpctl_dev_t *get_master_port_fn(bpctl_dev_t *pbpctl_dev)
{
	int idx_dev = 0;

	if (pbpctl_dev == NULL)
		return NULL;

	if ((pbpctl_dev->func == 1) || (pbpctl_dev->func == 3)) {
		for (idx_dev = 0;
		     ((bpctl_dev_arr[idx_dev].pdev != NULL)
		      && (idx_dev < device_num)); idx_dev++) {
			if ((bpctl_dev_arr[idx_dev].bus == pbpctl_dev->bus)
			    && (bpctl_dev_arr[idx_dev].slot == pbpctl_dev->slot)
			    && ((bpctl_dev_arr[idx_dev].func == 0)
				&& (pbpctl_dev->func == 1))) {

				return &(bpctl_dev_arr[idx_dev]);
			}
			if ((bpctl_dev_arr[idx_dev].bus == pbpctl_dev->bus) &&
			    (bpctl_dev_arr[idx_dev].slot == pbpctl_dev->slot) &&
			    ((bpctl_dev_arr[idx_dev].func == 2)
			     && (pbpctl_dev->func == 3))) {

				return &(bpctl_dev_arr[idx_dev]);
			}
		}
	}
	return NULL;
}

/**************************************/
/**************INTEL API***************/
/**************************************/

static void write_data_port_int(bpctl_dev_t *pbpctl_dev,
				unsigned char ctrl_value)
{
	uint32_t value;

	value = BPCTL_READ_REG(pbpctl_dev, CTRL);
/* Make SDP0 Pin Directonality to Output */
	value |= BPCTLI_CTRL_SDP0_DIR;
	BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL, value);

	value &= ~BPCTLI_CTRL_SDP0_DATA;
	value |= ((ctrl_value & 0x1) << BPCTLI_CTRL_SDP0_SHIFT);
	BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL, value);

	value = (BPCTL_READ_REG(pbpctl_dev, CTRL_EXT));
/* Make SDP2 Pin Directonality to Output */
	value |= BPCTLI_CTRL_EXT_SDP6_DIR;
	BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, value);

	value &= ~BPCTLI_CTRL_EXT_SDP6_DATA;
	value |= (((ctrl_value & 0x2) >> 1) << BPCTLI_CTRL_EXT_SDP6_SHIFT);
	BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT, value);

}

static int write_data_int(bpctl_dev_t *pbpctl_dev, unsigned char value)
{
	bpctl_dev_t *pbpctl_dev_b = NULL;

	if (!(pbpctl_dev_b = get_status_port_fn(pbpctl_dev)))
		return -1;
	atomic_set(&pbpctl_dev->wdt_busy, 1);
	write_data_port_int(pbpctl_dev, value & 0x3);
	write_data_port_int(pbpctl_dev_b, ((value & 0xc) >> 2));
	atomic_set(&pbpctl_dev->wdt_busy, 0);

	return 0;
}

static int wdt_pulse_int(bpctl_dev_t *pbpctl_dev)
{

	if ((atomic_read(&pbpctl_dev->wdt_busy)) == 1)
		return -1;

	if ((write_data_int(pbpctl_dev, RESET_WDT_INT)) < 0)
		return -1;
	msec_delay_bp(CMND_INTERVAL_INT);
	if ((write_data_int(pbpctl_dev, CMND_OFF_INT)) < 0)
		return -1;
	msec_delay_bp(CMND_INTERVAL_INT);

	if (pbpctl_dev->wdt_status == WDT_STATUS_EN)
		pbpctl_dev->bypass_wdt_on_time = jiffies;

	return 0;
}

/*************************************/
/************* COMMANDS **************/
/*************************************/

/* CMND_ON  0x4 (100)*/
int cmnd_on(bpctl_dev_t *pbpctl_dev)
{
	int ret = BP_NOT_CAP;

	if (pbpctl_dev->bp_caps & SW_CTL_CAP) {
		if (INTEL_IF_SERIES(pbpctl_dev->subdevice))
			return 0;
		if (pbpctl_dev->bp_ext_ver >= PXG2BPI_VER)
			write_data(pbpctl_dev, CMND_ON);
		else
			data_pulse(pbpctl_dev, CMND_ON);
		ret = 0;
	}
	return ret;
}

/* CMND_OFF  0x2 (10)*/
int cmnd_off(bpctl_dev_t *pbpctl_dev)
{
	int ret = BP_NOT_CAP;

	if (pbpctl_dev->bp_caps & SW_CTL_CAP) {
		if (INTEL_IF_SERIES(pbpctl_dev->subdevice)) {
			write_data_int(pbpctl_dev, CMND_OFF_INT);
			msec_delay_bp(CMND_INTERVAL_INT);
		} else if (pbpctl_dev->bp_ext_ver >= PXG2BPI_VER)
			write_data(pbpctl_dev, CMND_OFF);
		else
			data_pulse(pbpctl_dev, CMND_OFF);
		ret = 0;
	};
	return ret;
}

/* BYPASS_ON (0xa)*/
int bypass_on(bpctl_dev_t *pbpctl_dev)
{
	int ret = BP_NOT_CAP;

	if (pbpctl_dev->bp_caps & BP_CAP) {
		if (INTEL_IF_SERIES(pbpctl_dev->subdevice)) {
			write_data_int(pbpctl_dev, BYPASS_ON_INT);
			msec_delay_bp(BYPASS_DELAY_INT);
			pbpctl_dev->bp_status_un = 0;
		} else if (pbpctl_dev->bp_ext_ver >= PXG2BPI_VER) {
			write_data(pbpctl_dev, BYPASS_ON);
			if (pbpctl_dev->bp_ext_ver >= PXG2TBPI_VER)
				msec_delay_bp(LATCH_DELAY);
		} else
			data_pulse(pbpctl_dev, BYPASS_ON);
		ret = 0;
	};
	return ret;
}

/* BYPASS_OFF (0x8 111)*/
int bypass_off(bpctl_dev_t *pbpctl_dev)
{
	int ret = BP_NOT_CAP;

	if (pbpctl_dev->bp_caps & BP_CAP) {
		if (INTEL_IF_SERIES(pbpctl_dev->subdevice)) {
			write_data_int(pbpctl_dev, DIS_BYPASS_CAP_INT);
			msec_delay_bp(BYPASS_DELAY_INT);
			write_data_int(pbpctl_dev, PWROFF_BYPASS_ON_INT);
			msec_delay_bp(BYPASS_DELAY_INT);
			pbpctl_dev->bp_status_un = 0;
		} else if (pbpctl_dev->bp_ext_ver >= PXG2BPI_VER) {
			write_data(pbpctl_dev, BYPASS_OFF);
			if (pbpctl_dev->bp_ext_ver >= PXG2TBPI_VER)
				msec_delay_bp(LATCH_DELAY);
		} else
			data_pulse(pbpctl_dev, BYPASS_OFF);
		ret = 0;
	}
	return ret;
}

/* TAP_OFF (0x9)*/
int tap_off(bpctl_dev_t *pbpctl_dev)
{
	int ret = BP_NOT_CAP;
	if ((pbpctl_dev->bp_caps & TAP_CAP)
	    && (pbpctl_dev->bp_ext_ver >= PXG2TBPI_VER)) {
		write_data(pbpctl_dev, TAP_OFF);
		msec_delay_bp(LATCH_DELAY);
		ret = 0;
	};
	return ret;
}

/* TAP_ON (0xb)*/
int tap_on(bpctl_dev_t *pbpctl_dev)
{
	int ret = BP_NOT_CAP;
	if ((pbpctl_dev->bp_caps & TAP_CAP)
	    && (pbpctl_dev->bp_ext_ver >= PXG2TBPI_VER)) {
		write_data(pbpctl_dev, TAP_ON);
		msec_delay_bp(LATCH_DELAY);
		ret = 0;
	};
	return ret;
}

/* DISC_OFF (0x9)*/
int disc_off(bpctl_dev_t *pbpctl_dev)
{
	int ret = 0;
	if ((pbpctl_dev->bp_caps & DISC_CAP) && (pbpctl_dev->bp_ext_ver >= 0x8)) {
		write_data(pbpctl_dev, DISC_OFF);
		msec_delay_bp(LATCH_DELAY);
	} else
		ret = BP_NOT_CAP;
	return ret;
}

/* DISC_ON (0xb)*/
int disc_on(bpctl_dev_t *pbpctl_dev)
{
	int ret = 0;
	if ((pbpctl_dev->bp_caps & DISC_CAP) && (pbpctl_dev->bp_ext_ver >= 0x8)) {
		write_data(pbpctl_dev, /*DISC_ON */ 0x85);
		msec_delay_bp(LATCH_DELAY);
	} else
		ret = BP_NOT_CAP;
	return ret;
}

/* DISC_PORT_ON */
int disc_port_on(bpctl_dev_t *pbpctl_dev)
{
	int ret = 0;
	bpctl_dev_t *pbpctl_dev_m;

	if ((is_bypass_fn(pbpctl_dev)) == 1)
		pbpctl_dev_m = pbpctl_dev;
	else
		pbpctl_dev_m = get_master_port_fn(pbpctl_dev);
	if (pbpctl_dev_m == NULL)
		return BP_NOT_CAP;

	if (pbpctl_dev_m->bp_caps_ex & DISC_PORT_CAP_EX) {
		if (is_bypass_fn(pbpctl_dev) == 1) {

			write_data(pbpctl_dev_m, TX_DISA);
		} else {

			write_data(pbpctl_dev_m, TX_DISB);
		}

		msec_delay_bp(LATCH_DELAY);

	}
	return ret;
}

/* DISC_PORT_OFF */
int disc_port_off(bpctl_dev_t *pbpctl_dev)
{
	int ret = 0;
	bpctl_dev_t *pbpctl_dev_m;

	if ((is_bypass_fn(pbpctl_dev)) == 1)
		pbpctl_dev_m = pbpctl_dev;
	else
		pbpctl_dev_m = get_master_port_fn(pbpctl_dev);
	if (pbpctl_dev_m == NULL)
		return BP_NOT_CAP;

	if (pbpctl_dev_m->bp_caps_ex & DISC_PORT_CAP_EX) {
		if (is_bypass_fn(pbpctl_dev) == 1)
			write_data(pbpctl_dev_m, TX_ENA);
		else
			write_data(pbpctl_dev_m, TX_ENB);

		msec_delay_bp(LATCH_DELAY);

	}
	return ret;
}

/*TWO_PORT_LINK_HW_EN (0xe)*/
int tpl_hw_on(bpctl_dev_t *pbpctl_dev)
{
	int ret = 0, ctrl = 0;
	bpctl_dev_t *pbpctl_dev_b = NULL;

	if (!(pbpctl_dev_b = get_status_port_fn(pbpctl_dev)))
		return BP_NOT_CAP;

	if (pbpctl_dev->bp_caps_ex & TPL2_CAP_EX) {
		cmnd_on(pbpctl_dev);
		write_data(pbpctl_dev, TPL2_ON);
		msec_delay_bp(LATCH_DELAY + EEPROM_WR_DELAY);
		cmnd_off(pbpctl_dev);
		return ret;
	}

	if (TPL_IF_SERIES(pbpctl_dev->subdevice)) {
		ctrl = BPCTL_READ_REG(pbpctl_dev_b, CTRL);
		BPCTL_BP_WRITE_REG(pbpctl_dev_b, CTRL,
				   ((ctrl | BPCTLI_CTRL_SWDPIO0) &
				    ~BPCTLI_CTRL_SWDPIN0));
	} else
		ret = BP_NOT_CAP;
	return ret;
}

/*TWO_PORT_LINK_HW_DIS (0xc)*/
int tpl_hw_off(bpctl_dev_t *pbpctl_dev)
{
	int ret = 0, ctrl = 0;
	bpctl_dev_t *pbpctl_dev_b = NULL;

	if (!(pbpctl_dev_b = get_status_port_fn(pbpctl_dev)))
		return BP_NOT_CAP;
	if (pbpctl_dev->bp_caps_ex & TPL2_CAP_EX) {
		cmnd_on(pbpctl_dev);
		write_data(pbpctl_dev, TPL2_OFF);
		msec_delay_bp(LATCH_DELAY + EEPROM_WR_DELAY);
		cmnd_off(pbpctl_dev);
		return ret;
	}
	if (TPL_IF_SERIES(pbpctl_dev->subdevice)) {
		ctrl = BPCTL_READ_REG(pbpctl_dev_b, CTRL);
		BPCTL_BP_WRITE_REG(pbpctl_dev_b, CTRL,
				   (ctrl | BPCTLI_CTRL_SWDPIO0 |
				    BPCTLI_CTRL_SWDPIN0));
	} else
		ret = BP_NOT_CAP;
	return ret;
}

/* WDT_OFF (0x6 110)*/
int wdt_off(bpctl_dev_t *pbpctl_dev)
{
	int ret = BP_NOT_CAP;

	if (pbpctl_dev->bp_caps & WD_CTL_CAP) {
		if (INTEL_IF_SERIES(pbpctl_dev->subdevice)) {
			bypass_off(pbpctl_dev);
		} else if (pbpctl_dev->bp_ext_ver >= PXG2BPI_VER)
			write_data(pbpctl_dev, WDT_OFF);
		else
			data_pulse(pbpctl_dev, WDT_OFF);
		pbpctl_dev->wdt_status = WDT_STATUS_DIS;
		ret = 0;
	};
	return ret;
}

/* WDT_ON (0x10)*/

/***Global***/
static unsigned int
    wdt_val_array[] = { 1000, 1500, 2000, 3000, 4000, 8000, 16000, 32000, 0 };

int wdt_on(bpctl_dev_t *pbpctl_dev, unsigned int timeout)
{

	if (pbpctl_dev->bp_caps & WD_CTL_CAP) {
		unsigned int pulse = 0, temp_value = 0, temp_cnt = 0;
		pbpctl_dev->wdt_status = 0;

		if (INTEL_IF_SERIES(pbpctl_dev->subdevice)) {
			for (; wdt_val_array[temp_cnt]; temp_cnt++)
				if (timeout <= wdt_val_array[temp_cnt])
					break;

			if (!wdt_val_array[temp_cnt])
				temp_cnt--;

			timeout = wdt_val_array[temp_cnt];
			temp_cnt += 0x7;

			write_data_int(pbpctl_dev, DIS_BYPASS_CAP_INT);
			msec_delay_bp(BYPASS_DELAY_INT);
			pbpctl_dev->bp_status_un = 0;
			write_data_int(pbpctl_dev, temp_cnt);
			pbpctl_dev->bypass_wdt_on_time = jiffies;
			msec_delay_bp(CMND_INTERVAL_INT);
			pbpctl_dev->bypass_timer_interval = timeout;
		} else {
			timeout =
			    (timeout <
			     TIMEOUT_UNIT ? TIMEOUT_UNIT : (timeout >
							    WDT_TIMEOUT_MAX ?
							    WDT_TIMEOUT_MAX :
							    timeout));
			temp_value = timeout / 100;
			while ((temp_value >>= 1))
				temp_cnt++;
			if (timeout > ((1 << temp_cnt) * 100))
				temp_cnt++;
			pbpctl_dev->bypass_wdt_on_time = jiffies;
			pulse = (WDT_ON | temp_cnt);
			if (pbpctl_dev->bp_ext_ver == OLD_IF_VER)
				data_pulse(pbpctl_dev, pulse);
			else
				write_data(pbpctl_dev, pulse);
			pbpctl_dev->bypass_timer_interval =
			    (1 << temp_cnt) * 100;
		}
		pbpctl_dev->wdt_status = WDT_STATUS_EN;
		return 0;
	}
	return BP_NOT_CAP;
}

void bp75_put_hw_semaphore_generic(bpctl_dev_t *pbpctl_dev)
{
	u32 swsm;

	swsm = BPCTL_READ_REG(pbpctl_dev, SWSM);

	swsm &= ~(BPCTLI_SWSM_SMBI | BPCTLI_SWSM_SWESMBI);

	BPCTL_WRITE_REG(pbpctl_dev, SWSM, swsm);
}

s32 bp75_get_hw_semaphore_generic(bpctl_dev_t *pbpctl_dev)
{
	u32 swsm;
	s32 ret_val = 0;
	s32 timeout = 8192 + 1;
	s32 i = 0;

	/* Get the SW semaphore */
	while (i < timeout) {
		swsm = BPCTL_READ_REG(pbpctl_dev, SWSM);
		if (!(swsm & BPCTLI_SWSM_SMBI))
			break;

		usec_delay(50);
		i++;
	}

	if (i == timeout) {
		printk
		    ("bpctl_mod: Driver can't access device - SMBI bit is set.\n");
		ret_val = -1;
		goto out;
	}

	/* Get the FW semaphore. */
	for (i = 0; i < timeout; i++) {
		swsm = BPCTL_READ_REG(pbpctl_dev, SWSM);
		BPCTL_WRITE_REG(pbpctl_dev, SWSM, swsm | BPCTLI_SWSM_SWESMBI);

		/* Semaphore acquired if bit latched */
		if (BPCTL_READ_REG(pbpctl_dev, SWSM) & BPCTLI_SWSM_SWESMBI)
			break;

		usec_delay(50);
	}

	if (i == timeout) {
		/* Release semaphores */
		bp75_put_hw_semaphore_generic(pbpctl_dev);
		printk("bpctl_mod: Driver can't access the NVM\n");
		ret_val = -1;
		goto out;
	}

 out:
	return ret_val;
}

static void bp75_release_phy(bpctl_dev_t *pbpctl_dev)
{
	u16 mask = BPCTLI_SWFW_PHY0_SM;
	u32 swfw_sync;

	if ((pbpctl_dev->func == 1) || (pbpctl_dev->func == 3))
		mask = BPCTLI_SWFW_PHY1_SM;

	while (bp75_get_hw_semaphore_generic(pbpctl_dev) != 0) ;
	/* Empty */

	swfw_sync = BPCTL_READ_REG(pbpctl_dev, SW_FW_SYNC);
	swfw_sync &= ~mask;
	BPCTL_WRITE_REG(pbpctl_dev, SW_FW_SYNC, swfw_sync);

	bp75_put_hw_semaphore_generic(pbpctl_dev);
}

static s32 bp75_acquire_phy(bpctl_dev_t *pbpctl_dev)
{
	u16 mask = BPCTLI_SWFW_PHY0_SM;
	u32 swfw_sync;
	u32 swmask;
	u32 fwmask;
	s32 ret_val = 0;
	s32 i = 0, timeout = 200;

	if ((pbpctl_dev->func == 1) || (pbpctl_dev->func == 3))
		mask = BPCTLI_SWFW_PHY1_SM;

	swmask = mask;
	fwmask = mask << 16;

	while (i < timeout) {
		if (bp75_get_hw_semaphore_generic(pbpctl_dev)) {
			ret_val = -1;
			goto out;
		}

		swfw_sync = BPCTL_READ_REG(pbpctl_dev, SW_FW_SYNC);
		if (!(swfw_sync & (fwmask | swmask)))
			break;

		bp75_put_hw_semaphore_generic(pbpctl_dev);
		mdelay(5);
		i++;
	}

	if (i == timeout) {
		printk
		    ("bpctl_mod: Driver can't access resource, SW_FW_SYNC timeout.\n");
		ret_val = -1;
		goto out;
	}

	swfw_sync |= swmask;
	BPCTL_WRITE_REG(pbpctl_dev, SW_FW_SYNC, swfw_sync);

	bp75_put_hw_semaphore_generic(pbpctl_dev);

 out:
	return ret_val;
}

s32 bp75_read_phy_reg_mdic(bpctl_dev_t *pbpctl_dev, u32 offset, u16 *data)
{
	u32 i, mdic = 0;
	s32 ret_val = 0;
	u32 phy_addr = 1;

	mdic = ((offset << BPCTLI_MDIC_REG_SHIFT) |
		(phy_addr << BPCTLI_MDIC_PHY_SHIFT) | (BPCTLI_MDIC_OP_READ));

	BPCTL_WRITE_REG(pbpctl_dev, MDIC, mdic);

	for (i = 0; i < (BPCTLI_GEN_POLL_TIMEOUT * 3); i++) {
		usec_delay(50);
		mdic = BPCTL_READ_REG(pbpctl_dev, MDIC);
		if (mdic & BPCTLI_MDIC_READY)
			break;
	}
	if (!(mdic & BPCTLI_MDIC_READY)) {
		printk("bpctl_mod: MDI Read did not complete\n");
		ret_val = -1;
		goto out;
	}
	if (mdic & BPCTLI_MDIC_ERROR) {
		printk("bpctl_mod: MDI Error\n");
		ret_val = -1;
		goto out;
	}
	*data = (u16) mdic;

 out:
	return ret_val;
}

s32 bp75_write_phy_reg_mdic(bpctl_dev_t *pbpctl_dev, u32 offset, u16 data)
{
	u32 i, mdic = 0;
	s32 ret_val = 0;
	u32 phy_addr = 1;

	mdic = (((u32) data) |
		(offset << BPCTLI_MDIC_REG_SHIFT) |
		(phy_addr << BPCTLI_MDIC_PHY_SHIFT) | (BPCTLI_MDIC_OP_WRITE));

	BPCTL_WRITE_REG(pbpctl_dev, MDIC, mdic);

	for (i = 0; i < (BPCTLI_GEN_POLL_TIMEOUT * 3); i++) {
		usec_delay(50);
		mdic = BPCTL_READ_REG(pbpctl_dev, MDIC);
		if (mdic & BPCTLI_MDIC_READY)
			break;
	}
	if (!(mdic & BPCTLI_MDIC_READY)) {
		printk("bpctl_mod: MDI Write did not complete\n");
		ret_val = -1;
		goto out;
	}
	if (mdic & BPCTLI_MDIC_ERROR) {
		printk("bpctl_mod: MDI Error\n");
		ret_val = -1;
		goto out;
	}

 out:
	return ret_val;
}

static s32 bp75_read_phy_reg(bpctl_dev_t *pbpctl_dev, u32 offset, u16 *data)
{
	s32 ret_val = 0;

	ret_val = bp75_acquire_phy(pbpctl_dev);
	if (ret_val)
		goto out;

	if (offset > BPCTLI_MAX_PHY_MULTI_PAGE_REG) {
		ret_val = bp75_write_phy_reg_mdic(pbpctl_dev,
						  BPCTLI_IGP01E1000_PHY_PAGE_SELECT,
						  (u16) offset);
		if (ret_val)
			goto release;
	}

	ret_val =
	    bp75_read_phy_reg_mdic(pbpctl_dev,
				   BPCTLI_MAX_PHY_REG_ADDRESS & offset, data);

 release:
	bp75_release_phy(pbpctl_dev);
 out:
	return ret_val;
}

static s32 bp75_write_phy_reg(bpctl_dev_t *pbpctl_dev, u32 offset, u16 data)
{
	s32 ret_val = 0;

	ret_val = bp75_acquire_phy(pbpctl_dev);
	if (ret_val)
		goto out;

	if (offset > BPCTLI_MAX_PHY_MULTI_PAGE_REG) {
		ret_val = bp75_write_phy_reg_mdic(pbpctl_dev,
						  BPCTLI_IGP01E1000_PHY_PAGE_SELECT,
						  (u16) offset);
		if (ret_val)
			goto release;
	}

	ret_val =
	    bp75_write_phy_reg_mdic(pbpctl_dev,
				    BPCTLI_MAX_PHY_REG_ADDRESS & offset, data);

 release:
	bp75_release_phy(pbpctl_dev);

 out:
	return ret_val;
}

/* SET_TX  (non-Bypass command :)) */
static int set_tx(bpctl_dev_t *pbpctl_dev, int tx_state)
{
	int ret = 0, ctrl = 0;
	bpctl_dev_t *pbpctl_dev_m;
	if ((is_bypass_fn(pbpctl_dev)) == 1)
		pbpctl_dev_m = pbpctl_dev;
	else
		pbpctl_dev_m = get_master_port_fn(pbpctl_dev);
	if (pbpctl_dev_m == NULL)
		return BP_NOT_CAP;
	if (pbpctl_dev_m->bp_caps_ex & DISC_PORT_CAP_EX) {
		ctrl = BPCTL_READ_REG(pbpctl_dev, CTRL);
		if (!tx_state) {
			if (pbpctl_dev->bp_540) {
				ctrl = BP10G_READ_REG(pbpctl_dev, ESDP);
				BP10G_WRITE_REG(pbpctl_dev, ESDP,
						(ctrl | BP10G_SDP1_DIR |
						 BP10G_SDP1_DATA));

			} else {
				BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL,
						   (ctrl | BPCTLI_CTRL_SDP1_DIR
						    | BPCTLI_CTRL_SWDPIN1));
			}
		} else {
			if (pbpctl_dev->bp_540) {
				ctrl = BP10G_READ_REG(pbpctl_dev, ESDP);
				BP10G_WRITE_REG(pbpctl_dev, ESDP,
						((ctrl | BP10G_SDP1_DIR) &
						 ~BP10G_SDP1_DATA));
			} else {
				BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL,
						   ((ctrl |
						     BPCTLI_CTRL_SDP1_DIR) &
						    ~BPCTLI_CTRL_SWDPIN1));
			}
			return ret;

		}
	} else if (pbpctl_dev->bp_caps & TX_CTL_CAP) {
		if (PEG5_IF_SERIES(pbpctl_dev->subdevice)) {
			if (tx_state) {
				uint16_t mii_reg;
				if (!
				    (ret =
				     bp75_read_phy_reg(pbpctl_dev,
						       BPCTLI_PHY_CONTROL,
						       &mii_reg))) {
					if (mii_reg & BPCTLI_MII_CR_POWER_DOWN) {
						ret =
						    bp75_write_phy_reg
						    (pbpctl_dev,
						     BPCTLI_PHY_CONTROL,
						     mii_reg &
						     ~BPCTLI_MII_CR_POWER_DOWN);
					}
				}
			} else {
				uint16_t mii_reg;
				if (!
				    (ret =
				     bp75_read_phy_reg(pbpctl_dev,
						       BPCTLI_PHY_CONTROL,
						       &mii_reg))) {

					mii_reg |= BPCTLI_MII_CR_POWER_DOWN;
					ret =
					    bp75_write_phy_reg(pbpctl_dev,
							       BPCTLI_PHY_CONTROL,
							       mii_reg);
				}
			}

		}
		if (pbpctl_dev->bp_fiber5) {
			ctrl = BPCTL_READ_REG(pbpctl_dev, CTRL_EXT);

		} else if (pbpctl_dev->bp_10gb)
			ctrl = BP10GB_READ_REG(pbpctl_dev, MISC_REG_GPIO);

		else if (!pbpctl_dev->bp_10g)
			ctrl = BPCTL_READ_REG(pbpctl_dev, CTRL);
		else
			ctrl = BP10G_READ_REG(pbpctl_dev, ESDP);

		if (!tx_state)
			if (pbpctl_dev->bp_10g9) {
				BP10G_WRITE_REG(pbpctl_dev, ESDP,
						(ctrl | BP10G_SDP3_DATA |
						 BP10G_SDP3_DIR));

			} else if (pbpctl_dev->bp_fiber5) {
				BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT,
						   (ctrl |
						    BPCTLI_CTRL_EXT_SDP6_DIR |
						    BPCTLI_CTRL_EXT_SDP6_DATA));

			} else if (pbpctl_dev->bp_10gb) {
				if ((pbpctl_dev->func == 1)
				    || (pbpctl_dev->func == 3))
					BP10GB_WRITE_REG(pbpctl_dev,
							 MISC_REG_GPIO,
							 (ctrl |
							  BP10GB_GPIO0_SET_P1) &
							 ~(BP10GB_GPIO0_CLR_P1 |
							   BP10GB_GPIO0_OE_P1));
				else
					BP10GB_WRITE_REG(pbpctl_dev,
							 MISC_REG_GPIO,
							 (ctrl |
							  BP10GB_GPIO0_OE_P0 |
							  BP10GB_GPIO0_SET_P0));

			} else if (pbpctl_dev->bp_i80) {
				BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL,
						   (ctrl | BPCTLI_CTRL_SDP1_DIR
						    | BPCTLI_CTRL_SWDPIN1));

			} else if (pbpctl_dev->bp_540) {
				ctrl = BP10G_READ_REG(pbpctl_dev, ESDP);
				BP10G_WRITE_REG(pbpctl_dev, ESDP,
						(ctrl | BP10G_SDP1_DIR |
						 BP10G_SDP1_DATA));

			}

			else if (!pbpctl_dev->bp_10g)
				BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL,
						   (ctrl | BPCTLI_CTRL_SWDPIO0 |
						    BPCTLI_CTRL_SWDPIN0));

			else
				BP10G_WRITE_REG(pbpctl_dev, ESDP,
						(ctrl | BP10G_SDP0_DATA |
						 BP10G_SDP0_DIR));

		else {
			if (pbpctl_dev->bp_10g9) {
				BP10G_WRITE_REG(pbpctl_dev, ESDP,
						((ctrl | BP10G_SDP3_DIR) &
						 ~BP10G_SDP3_DATA));

			} else if (pbpctl_dev->bp_fiber5) {
				BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL_EXT,
						   ((ctrl |
						     BPCTLI_CTRL_EXT_SDP6_DIR) &
						    ~BPCTLI_CTRL_EXT_SDP6_DATA));

			} else if (pbpctl_dev->bp_10gb) {
				if ((bpctl_dev_arr->func == 1)
				    || (bpctl_dev_arr->func == 3))
					BP10GB_WRITE_REG(pbpctl_dev,
							 MISC_REG_GPIO,
							 (ctrl |
							  BP10GB_GPIO0_CLR_P1) &
							 ~(BP10GB_GPIO0_SET_P1 |
							   BP10GB_GPIO0_OE_P1));
				else
					BP10GB_WRITE_REG(pbpctl_dev,
							 MISC_REG_GPIO,
							 (ctrl |
							  BP10GB_GPIO0_OE_P0 |
							  BP10GB_GPIO0_CLR_P0));

			} else if (pbpctl_dev->bp_i80) {
				BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL,
						   ((ctrl |
						     BPCTLI_CTRL_SDP1_DIR) &
						    ~BPCTLI_CTRL_SWDPIN1));
			} else if (pbpctl_dev->bp_540) {
				ctrl = BP10G_READ_REG(pbpctl_dev, ESDP);
				BP10G_WRITE_REG(pbpctl_dev, ESDP,
						((ctrl | BP10G_SDP1_DIR) &
						 ~BP10G_SDP1_DATA));
			}

			else if (!pbpctl_dev->bp_10g) {
				BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL,
						   ((ctrl | BPCTLI_CTRL_SWDPIO0)
						    & ~BPCTLI_CTRL_SWDPIN0));
				if (!PEGF_IF_SERIES(pbpctl_dev->subdevice)) {
					BPCTL_BP_WRITE_REG(pbpctl_dev, CTRL,
							   (ctrl &
							    ~
							    (BPCTLI_CTRL_SDP0_DATA
							     |
							     BPCTLI_CTRL_SDP0_DIR)));
				}
			} else
				BP10G_WRITE_REG(pbpctl_dev, ESDP,
						((ctrl | BP10G_SDP0_DIR) &
						 ~BP10G_SDP0_DATA));

		}

	} else
		ret = BP_NOT_CAP;
	return ret;

}

/* SET_FORCE_LINK  (non-Bypass command :)) */
static int set_bp_force_link(bpctl_dev_t *pbpctl_dev, int tx_state)
{
	int ret = 0, ctrl = 0;

	if (DBI_IF_SERIES(pbpctl_dev->subdevice)) {

		if ((pbpctl_dev->bp_10g) || (pbpctl_dev->bp_10g9)) {

			ctrl = BPCTL_READ_REG(pbpctl_dev, CTRL);
			if (!tx_state)
				BP10G_WRITE_REG(pbpctl_dev, ESDP,
						ctrl & ~BP10G_SDP1_DIR);
			else
				BP10G_WRITE_REG(pbpctl_dev, ESDP,
						((ctrl | BP10G_SDP1_DIR) &
						 ~BP10G_SDP1_DATA));
			return ret;
		}

	}
	return BP_NOT_CAP;
}

/*RESET_CONT 0x20 */
int reset_cont(bpctl_dev_t *pbpctl_dev)
{
	int ret = BP_NOT_CAP;

	if (pbpctl_dev->bp_caps & SW_CTL_CAP) {
		if (INTEL_IF_SERIES(pbpctl_dev->subdevice))
			return BP_NOT_CAP;
		if (pbpctl_dev->bp_ext_ver >= PXG2BPI_VER)
			write_data(pbpctl_dev, RESET_CONT);
		else
			data_pulse(pbpctl_dev, RESET_CONT);
		ret = 0;
	};
	return ret;
}

/*DIS_BYPASS_CAP 0x22 */
int dis_bypass_cap(bpctl_dev_t *pbpctl_dev)
{

	if (pbpctl_dev->bp_caps & BP_DIS_CAP) {
		if (INTEL_IF_SERIES(pbpctl_dev->subdevice)) {
			write_data_int(pbpctl_dev, DIS_BYPASS_CAP_INT);
			msec_delay_bp(BYPASS_DELAY_INT);
		} else {
			write_data(pbpctl_dev, BYPASS_OFF);
			msec_delay_bp(LATCH_DELAY);
			write_data(pbpctl_dev, DIS_BYPASS_CAP);
			msec_delay_bp(BYPASS_CAP_DELAY);
		}
		return 0;
	}
	return BP_NOT_CAP;
}

/*EN_BYPASS_CAP 0x24 */
int en_bypass_cap(bpctl_dev_t *pbpctl_dev)
{
	if (pbpctl_dev->bp_caps & BP_DIS_CAP) {
		if (INTEL_IF_SERIES(pbpctl_dev->subdevice)) {
			write_data_int(pbpctl_dev, PWROFF_BYPASS_ON_INT);
			msec_delay_bp(BYPASS_DELAY_INT);
		} else {
			write_data(pbpctl_dev, EN_BYPASS_CAP);
			msec_delay_bp(BYPASS_CAP_DELAY);
		}
		return 0;
	}
	return BP_NOT_CAP;
}

/* BYPASS_STATE_PWRON 0x26*/
int bypass_state_pwron(bpctl_dev_t *pbpctl_dev)
{
	if (pbpctl_dev->bp_caps & BP_PWUP_CTL_CAP) {
		write_data(pbpctl_dev, BYPASS_STATE_PWRON);
		if (pbpctl_dev->bp_ext_ver == PXG2BPI_VER)
			msec_delay_bp(DFLT_PWRON_DELAY);
		else
			msec_delay_bp(LATCH_DELAY + EEPROM_WR_DELAY);
		return 0;
	}
	return BP_NOT_CAP;
}

/* NORMAL_STATE_PWRON 0x28*/
int normal_state_pwron(bpctl_dev_t *pbpctl_dev)
{
	if ((pbpctl_dev->bp_caps & BP_PWUP_CTL_CAP)
	    || (pbpctl_dev->bp_caps & TAP_PWUP_CTL_CAP)) {
		write_data(pbpctl_dev, NORMAL_STATE_PWRON);
		if (pbpctl_dev->bp_ext_ver == PXG2BPI_VER)
			msec_delay_bp(DFLT_PWRON_DELAY);
		else
			msec_delay_bp(LATCH_DELAY + EEPROM_WR_DELAY);
		return 0;
	}
	return BP_NOT_CAP;
}

/* BYPASS_STATE_PWROFF 0x27*/
int bypass_state_pwroff(bpctl_dev_t *pbpctl_dev)
{
	if (pbpctl_dev->bp_caps & BP_PWOFF_CTL_CAP) {
		write_data(pbpctl_dev, BYPASS_STATE_PWROFF);
		msec_delay_bp(LATCH_DELAY + EEPROM_WR_DELAY);
		return 0;
	}
	return BP_NOT_CAP;
}

/* NORMAL_STATE_PWROFF 0x29*/
int normal_state_pwroff(bpctl_dev_t *pbpctl_dev)
{
	if ((pbpctl_dev->bp_caps & BP_PWOFF_CTL_CAP)) {
		write_data(pbpctl_dev, NORMAL_STATE_PWROFF);
		msec_delay_bp(LATCH_DELAY + EEPROM_WR_DELAY);
		return 0;
	}
	return BP_NOT_CAP;
}

/*TAP_STATE_PWRON 0x2a*/
int tap_state_pwron(bpctl_dev_t *pbpctl_dev)
{
	if (pbpctl_dev->bp_caps & TAP_PWUP_CTL_CAP) {
		write_data(pbpctl_dev, TAP_STATE_PWRON);
		msec_delay_bp(LATCH_DELAY + EEPROM_WR_DELAY);
		return 0;
	}
	return BP_NOT_CAP;
}

/*DIS_TAP_CAP 0x2c*/
int dis_tap_cap(bpctl_dev_t *pbpctl_dev)
{
	if (pbpctl_dev->bp_caps & TAP_DIS_CAP) {
		write_data(pbpctl_dev, DIS_TAP_CAP);
		msec_delay_bp(BYPASS_CAP_DELAY);
		return 0;
	}
	return BP_NOT_CAP;
}

/*EN_TAP_CAP 0x2e*/
int en_tap_cap(bpctl_dev_t *pbpctl_dev)
{
	if (pbpctl_dev->bp_caps & TAP_DIS_CAP) {
		write_data(pbpctl_dev, EN_TAP_CAP);
		msec_delay_bp(BYPASS_CAP_DELAY);
		return 0;
	}
	return BP_NOT_CAP;
}

/*DISC_STATE_PWRON 0x2a*/
int disc_state_pwron(bpctl_dev_t *pbpctl_dev)
{
	if (pbpctl_dev->bp_caps & DISC_PWUP_CTL_CAP) {
		if (pbpctl_dev->bp_ext_ver >= 0x8) {
			write_data(pbpctl_dev, DISC_STATE_PWRON);
			msec_delay_bp(LATCH_DELAY + EEPROM_WR_DELAY);
			return BP_OK;
		}
	}
	return BP_NOT_CAP;
}

/*DIS_DISC_CAP 0x2c*/
int dis_disc_cap(bpctl_dev_t *pbpctl_dev)
{
	if (pbpctl_dev->bp_caps & DISC_DIS_CAP) {
		if (pbpctl_dev->bp_ext_ver >= 0x8) {
			write_data(pbpctl_dev, DIS_DISC_CAP);
			msec_delay_bp(BYPASS_CAP_DELAY);
			return BP_OK;
		}
	}
	return BP_NOT_CAP;
}

/*DISC_STATE_PWRON 0x2a*/
int disc_port_state_pwron(bpctl_dev_t *pbpctl_dev)
{
	int ret = 0;
	bpctl_dev_t *pbpctl_dev_m;

	return BP_NOT_CAP;

	if ((is_bypass_fn(pbpctl_dev)) == 1)
		pbpctl_dev_m = pbpctl_dev;
	else
		pbpctl_dev_m = get_master_port_fn(pbpctl_dev);
	if (pbpctl_dev_m == NULL)
		return BP_NOT_CAP;

	if (pbpctl_dev_m->bp_caps_ex & DISC_PORT_CAP_EX) {
		if (is_bypass_fn(pbpctl_dev) == 1)
			write_data(pbpctl_dev_m, TX_DISA_PWRUP);
		else
			write_data(pbpctl_dev_m, TX_DISB_PWRUP);

		msec_delay_bp(LATCH_DELAY);

	}
	return ret;
}

int normal_port_state_pwron(bpctl_dev_t *pbpctl_dev)
{
	int ret = 0;
	bpctl_dev_t *pbpctl_dev_m;
	return BP_NOT_CAP;

	if ((is_bypass_fn(pbpctl_dev)) == 1)
		pbpctl_dev_m = pbpctl_dev;
	else
		pbpctl_dev_m = get_master_port_fn(pbpctl_dev);
	if (pbpctl_dev_m == NULL)
		return BP_NOT_CAP;

	if (pbpctl_dev_m->bp_caps_ex & DISC_PORT_CAP_EX) {
		if (is_bypass_fn(pbpctl_dev) == 1)
			write_data(pbpctl_dev_m, TX_ENA_PWRUP);
		else
			write_data(pbpctl_dev_m, TX_ENB_PWRUP);

		msec_delay_bp(LATCH_DELAY);

	}
	return ret;
}

/*EN_TAP_CAP 0x2e*/
int en_disc_cap(bpctl_dev_t *pbpctl_dev)
{
	if (pbpctl_dev->bp_caps & DISC_DIS_CAP) {
		if (pbpctl_dev->bp_ext_ver >= 0x8) {
			write_data(pbpctl_dev, EN_DISC_CAP);
			msec_delay_bp(BYPASS_CAP_DELAY);
			return BP_OK;
		}
	}
	return BP_NOT_CAP;
}

int std_nic_on(bpctl_dev_t *pbpctl_dev)
{

	if (pbpctl_dev->bp_caps & STD_NIC_CAP) {

		if (INTEL_IF_SERIES(pbpctl_dev->subdevice)) {
			write_data_int(pbpctl_dev, DIS_BYPASS_CAP_INT);
			msec_delay_bp(BYPASS_DELAY_INT);
			pbpctl_dev->bp_status_un = 0;
			return BP_OK;
		}

		if (pbpctl_dev->bp_ext_ver >= 0x8) {
			write_data(pbpctl_dev, STD_NIC_ON);
			msec_delay_bp(BYPASS_CAP_DELAY);
			return BP_OK;

		}

		if (pbpctl_dev->bp_ext_ver >= PXG2BPI_VER) {
			wdt_off(pbpctl_dev);

			if (pbpctl_dev->bp_caps & BP_CAP) {
				write_data(pbpctl_dev, BYPASS_OFF);
				msec_delay_bp(LATCH_DELAY);
			}

			if (pbpctl_dev->bp_caps & TAP_CAP) {
				write_data(pbpctl_dev, TAP_OFF);
				msec_delay_bp(LATCH_DELAY);
			}

			write_data(pbpctl_dev, NORMAL_STATE_PWRON);
			if (pbpctl_dev->bp_ext_ver == PXG2BPI_VER)
				msec_delay_bp(DFLT_PWRON_DELAY);
			else
				msec_delay_bp(LATCH_DELAY + EEPROM_WR_DELAY);

			if (pbpctl_dev->bp_caps & BP_DIS_CAP) {
				write_data(pbpctl_dev, DIS_BYPASS_CAP);
				msec_delay_bp(BYPASS_CAP_DELAY);
			}

			if (pbpctl_dev->bp_caps & TAP_DIS_CAP) {
				write_data(pbpctl_dev, DIS_TAP_CAP);
				msec_delay_bp(BYPASS_CAP_DELAY);

			}
			return 0;
		}
	}
	return BP_NOT_CAP;
}

int std_nic_off(bpctl_dev_t *pbpctl_dev)
{

	if (pbpctl_dev->bp_caps & STD_NIC_CAP) {
		if (INTEL_IF_SERIES(pbpctl_dev->subdevice)) {
			write_data_int(pbpctl_dev, PWROFF_BYPASS_ON_INT);
			msec_delay_bp(BYPASS_DELAY_INT);
			return BP_OK;
		}
		if (pbpctl_dev->bp_ext_ver >= 0x8) {
			write_data(pbpctl_dev, STD_NIC_OFF);
			msec_delay_bp(BYPASS_CAP_DELAY);
			return BP_OK;

		}

		if (pbpctl_dev->bp_ext_ver >= PXG2BPI_VER) {

			if (pbpctl_dev->bp_caps & TAP_PWUP_CTL_CAP) {
				write_data(pbpctl_dev, TAP_STATE_PWRON);
				msec_delay_bp(LATCH_DELAY + EEPROM_WR_DELAY);
			}

			if (pbpctl_dev->bp_caps & BP_PWUP_CTL_CAP) {
				write_data(pbpctl_dev, BYPASS_STATE_PWRON);
				if (pbpctl_dev->bp_ext_ver > PXG2BPI_VER)
					msec_delay_bp(LATCH_DELAY +
						      EEPROM_WR_DELAY);
				else
					msec_delay_bp(DFLT_PWRON_DELAY);
			}

			if (pbpctl_dev->bp_caps & TAP_DIS_CAP) {
				write_data(pbpctl_dev, EN_TAP_CAP);
				msec_delay_bp(BYPASS_CAP_DELAY);
			}
			if (pbpctl_dev->bp_caps & DISC_DIS_CAP) {
				write_data(pbpctl_dev, EN_DISC_CAP);
				msec_delay_bp(BYPASS_CAP_DELAY);
			}

			if (pbpctl_dev->bp_caps & BP_DIS_CAP) {
				write_data(pbpctl_dev, EN_BYPASS_CAP);
				msec_delay_bp(BYPASS_CAP_DELAY);
			}

			return 0;
		}
	}
	return BP_NOT_CAP;
}

int wdt_time_left(bpctl_dev_t *pbpctl_dev)
{

	/* unsigned long curr_time=((long long)(jiffies*1000))/HZ, delta_time=0,wdt_on_time=((long long)(pbpctl_dev->bypass_wdt_on_time*1000))/HZ; */
	unsigned long curr_time = jiffies, delta_time = 0, wdt_on_time =
	    pbpctl_dev->bypass_wdt_on_time, delta_time_msec = 0;
	int time_left = 0;

	switch (pbpctl_dev->wdt_status) {
	case WDT_STATUS_DIS:
		time_left = 0;
		break;
	case WDT_STATUS_EN:
		delta_time =
		    (curr_time >=
		     wdt_on_time) ? (curr_time - wdt_on_time) : (~wdt_on_time +
								 curr_time);
		delta_time_msec = jiffies_to_msecs(delta_time);
		time_left = pbpctl_dev->bypass_timer_interval - delta_time_msec;
		if (time_left < 0) {
			time_left = -1;
			pbpctl_dev->wdt_status = WDT_STATUS_EXP;
		}
		break;
	case WDT_STATUS_EXP:
		time_left = -1;
		break;
	}

	return time_left;
}

static int wdt_timer(bpctl_dev_t *pbpctl_dev, int *time_left)
{
	int ret = 0;
	if (pbpctl_dev->bp_caps & WD_CTL_CAP) {
		{
			if (pbpctl_dev->wdt_status == WDT_STATUS_UNKNOWN)
				ret = BP_NOT_CAP;
			else
				*time_left = wdt_time_left(pbpctl_dev);
		}

	} else
		ret = BP_NOT_CAP;
	return ret;
}

static int wdt_timer_reload(bpctl_dev_t *pbpctl_dev)
{

	int ret = 0;

	if ((pbpctl_dev->bp_caps & WD_CTL_CAP) &&
	    (pbpctl_dev->wdt_status != WDT_STATUS_UNKNOWN)) {
		if (pbpctl_dev->wdt_status == WDT_STATUS_DIS)
			return 0;
		if (pbpctl_dev->bp_ext_ver >= PXG2BPI_VER)
			ret = wdt_pulse(pbpctl_dev);
		else if (INTEL_IF_SERIES(pbpctl_dev->subdevice))
			ret = wdt_pulse_int(pbpctl_dev);
		else
			ret = send_wdt_pulse(pbpctl_dev);
		/* if (ret==-1)
		    mod_timer(&pbpctl_dev->bp_timer, jiffies+1);*/
		return 1;
	}
	return BP_NOT_CAP;
}

static void wd_reset_timer(unsigned long param)
{
	bpctl_dev_t *pbpctl_dev = (bpctl_dev_t *) param;
#ifdef BP_SELF_TEST
	struct sk_buff *skb_tmp;
#endif

	if ((pbpctl_dev->bp_ext_ver >= PXG2BPI_VER) &&
	    ((atomic_read(&pbpctl_dev->wdt_busy)) == 1)) {
		mod_timer(&pbpctl_dev->bp_timer, jiffies + 1);
		return;
	}
#ifdef BP_SELF_TEST

	if (pbpctl_dev->bp_self_test_flag == 1) {
		skb_tmp = dev_alloc_skb(BPTEST_DATA_LEN + 2);
		if ((skb_tmp) && (pbpctl_dev->ndev) && (pbpctl_dev->bp_tx_data)) {
			memcpy(skb_put(skb_tmp, BPTEST_DATA_LEN),
			       pbpctl_dev->bp_tx_data, BPTEST_DATA_LEN);
			skb_tmp->dev = pbpctl_dev->ndev;
			skb_tmp->protocol =
			    eth_type_trans(skb_tmp, pbpctl_dev->ndev);
			skb_tmp->ip_summed = CHECKSUM_UNNECESSARY;
			netif_receive_skb(skb_tmp);
			goto bp_timer_reload;
			return;
		}
	}
#endif

	wdt_timer_reload(pbpctl_dev);
#ifdef BP_SELF_TEST
 bp_timer_reload:
#endif
	if (pbpctl_dev->reset_time) {
		mod_timer(&pbpctl_dev->bp_timer,
			  jiffies + (HZ * pbpctl_dev->reset_time) / 1000);
	}
}

/*WAIT_AT_PWRUP 0x80   */
int bp_wait_at_pwup_en(bpctl_dev_t *pbpctl_dev)
{

	if (pbpctl_dev->bp_caps & SW_CTL_CAP) {
		if (pbpctl_dev->bp_ext_ver >= BP_FW_EXT_VER8) {
			write_data(pbpctl_dev, BP_WAIT_AT_PWUP_EN);
			msec_delay_bp(LATCH_DELAY + EEPROM_WR_DELAY);

			return BP_OK;
		}
	}
	return BP_NOT_CAP;
}

/*DIS_WAIT_AT_PWRUP       0x81 */
int bp_wait_at_pwup_dis(bpctl_dev_t *pbpctl_dev)
{

	if (pbpctl_dev->bp_caps & SW_CTL_CAP) {

		if (pbpctl_dev->bp_ext_ver >= BP_FW_EXT_VER8) {
			write_data(pbpctl_dev, BP_WAIT_AT_PWUP_DIS);
			msec_delay_bp(LATCH_DELAY + EEPROM_WR_DELAY);

			return BP_OK;
		}
	}
	return BP_NOT_CAP;
}

/*EN_HW_RESET  0x82   */

int bp_hw_reset_en(bpctl_dev_t *pbpctl_dev)
{

	if (pbpctl_dev->bp_caps & SW_CTL_CAP) {
		if (pbpctl_dev->bp_ext_ver >= BP_FW_EXT_VER8) {
			write_data(pbpctl_dev, BP_HW_RESET_EN);
			msec_delay_bp(LATCH_DELAY + EEPROM_WR_DELAY);

			return BP_OK;
		}
	}
	return BP_NOT_CAP;
}

/*DIS_HW_RESET             0x83   */

int bp_hw_reset_dis(bpctl_dev_t *pbpctl_dev)
{

	if (pbpctl_dev->bp_caps & SW_CTL_CAP) {
		if (pbpctl_dev->bp_ext_ver >= BP_FW_EXT_VER8) {
			write_data(pbpctl_dev, BP_HW_RESET_DIS);
			msec_delay_bp(LATCH_DELAY + EEPROM_WR_DELAY);

			return BP_OK;
		}
	}
	return BP_NOT_CAP;
}


int wdt_exp_mode(bpctl_dev_t *pbpctl_dev, int mode)
{
	uint32_t status_reg = 0, status_reg1 = 0;

	if ((pbpctl_dev->bp_caps & (TAP_STATUS_CAP | DISC_CAP)) &&
	    (pbpctl_dev->bp_caps & BP_CAP)) {
		if (pbpctl_dev->bp_ext_ver >= PXE2TBPI_VER) {

			if ((pbpctl_dev->bp_ext_ver >= 0x8) &&
			    (mode == 2) && (pbpctl_dev->bp_caps & DISC_CAP)) {
				status_reg1 =
				    read_reg(pbpctl_dev, STATUS_DISC_REG_ADDR);
				if (!(status_reg1 & WDTE_DISC_BPN_MASK))
					write_reg(pbpctl_dev,
						  status_reg1 |
						  WDTE_DISC_BPN_MASK,
						  STATUS_DISC_REG_ADDR);
				return BP_OK;
			}
		}
		status_reg = read_reg(pbpctl_dev, STATUS_TAP_REG_ADDR);

		if ((mode == 0) && (pbpctl_dev->bp_caps & BP_CAP)) {
			if (pbpctl_dev->bp_ext_ver >= 0x8) {
				status_reg1 =
				    read_reg(pbpctl_dev, STATUS_DISC_REG_ADDR);
				if (status_reg1 & WDTE_DISC_BPN_MASK)
					write_reg(pbpctl_dev,
						  status_reg1 &
						  ~WDTE_DISC_BPN_MASK,
						  STATUS_DISC_REG_ADDR);
			}
			if (status_reg & WDTE_TAP_BPN_MASK)
				write_reg(pbpctl_dev,
					  status_reg & ~WDTE_TAP_BPN_MASK,
					  STATUS_TAP_REG_ADDR);
			return BP_OK;

		} else if ((mode == 1) && (pbpctl_dev->bp_caps & TAP_CAP)) {
			if (!(status_reg & WDTE_TAP_BPN_MASK))
				write_reg(pbpctl_dev,
					  status_reg | WDTE_TAP_BPN_MASK,
					  STATUS_TAP_REG_ADDR);
			/*else return BP_NOT_CAP; */
			return BP_OK;
		}

	}
	return BP_NOT_CAP;
}

int bypass_fw_ver(bpctl_dev_t *pbpctl_dev)
{
	if (is_bypass_fn(pbpctl_dev))
		return read_reg(pbpctl_dev, VER_REG_ADDR);
	else
		return BP_NOT_CAP;
}

int bypass_sign_check(bpctl_dev_t *pbpctl_dev)
{

	if (is_bypass_fn(pbpctl_dev))
		return (((read_reg(pbpctl_dev, PIC_SIGN_REG_ADDR)) ==
			 PIC_SIGN_VALUE) ? 1 : 0);
	else
		return BP_NOT_CAP;
}

static int tx_status(bpctl_dev_t *pbpctl_dev)
{
	uint32_t ctrl = 0;
	bpctl_dev_t *pbpctl_dev_m;
	if ((is_bypass_fn(pbpctl_dev)) == 1)
		pbpctl_dev_m = pbpctl_dev;
	else
		pbpctl_dev_m = get_master_port_fn(pbpctl_dev);
	if (pbpctl_dev_m == NULL)
		return BP_NOT_CAP;
	if (pbpctl_dev_m->bp_caps_ex & DISC_PORT_CAP_EX) {

		ctrl = BPCTL_READ_REG(pbpctl_dev, CTRL);
		if (pbpctl_dev->bp_i80)
			return ((ctrl & BPCTLI_CTRL_SWDPIN1) != 0 ? 0 : 1);
		if (pbpctl_dev->bp_540) {
			ctrl = BP10G_READ_REG(pbpctl_dev, ESDP);

			return ((ctrl & BP10G_SDP1_DATA) != 0 ? 0 : 1);
		}

	}

	if (pbpctl_dev->bp_caps & TX_CTL_CAP) {
		if (PEG5_IF_SERIES(pbpctl_dev->subdevice)) {
			uint16_t mii_reg;
			if (!
			    (bp75_read_phy_reg
			     (pbpctl_dev, BPCTLI_PHY_CONTROL, &mii_reg))) {
				if (mii_reg & BPCTLI_MII_CR_POWER_DOWN)
					return 0;

				else
					return 1;
			}
			return -1;
		}

		if (pbpctl_dev->bp_10g9) {
			return ((BP10G_READ_REG(pbpctl_dev, ESDP) &
				 BP10G_SDP3_DATA) != 0 ? 0 : 1);

		} else if (pbpctl_dev->bp_fiber5) {
			ctrl = BPCTL_READ_REG(pbpctl_dev, CTRL_EXT);
			if (ctrl & BPCTLI_CTRL_EXT_SDP6_DATA)
				return 0;
			return 1;
		} else if (pbpctl_dev->bp_10gb) {
			ctrl = BP10GB_READ_REG(pbpctl_dev, MISC_REG_GPIO);
			BP10GB_WRITE_REG(pbpctl_dev, MISC_REG_GPIO,
					 (ctrl | BP10GB_GPIO0_OE_P1) &
					 ~(BP10GB_GPIO0_SET_P1 |
					   BP10GB_GPIO0_CLR_P1));

			if ((pbpctl_dev->func == 1) || (pbpctl_dev->func == 3))
				return (((BP10GB_READ_REG
					  (pbpctl_dev,
					   MISC_REG_GPIO)) & BP10GB_GPIO0_P1) !=
					0 ? 0 : 1);
			else
				return (((BP10GB_READ_REG
					  (pbpctl_dev,
					   MISC_REG_GPIO)) & BP10GB_GPIO0_P0) !=
					0 ? 0 : 1);
		}

		if (!pbpctl_dev->bp_10g) {

			ctrl = BPCTL_READ_REG(pbpctl_dev, CTRL);
			if (pbpctl_dev->bp_i80)
				return ((ctrl & BPCTLI_CTRL_SWDPIN1) !=
					0 ? 0 : 1);
			if (pbpctl_dev->bp_540) {
				ctrl = BP10G_READ_REG(pbpctl_dev, ESDP);

				return ((ctrl & BP10G_SDP1_DATA) != 0 ? 0 : 1);
			}

			return ((ctrl & BPCTLI_CTRL_SWDPIN0) != 0 ? 0 : 1);
		} else
			return ((BP10G_READ_REG(pbpctl_dev, ESDP) &
				 BP10G_SDP0_DATA) != 0 ? 0 : 1);

	}
	return BP_NOT_CAP;
}

static int bp_force_link_status(bpctl_dev_t *pbpctl_dev)
{

	if (DBI_IF_SERIES(pbpctl_dev->subdevice)) {

		if ((pbpctl_dev->bp_10g) || (pbpctl_dev->bp_10g9)) {
			return ((BP10G_READ_REG(pbpctl_dev, ESDP) &
				 BP10G_SDP1_DIR) != 0 ? 1 : 0);

		}
	}
	return BP_NOT_CAP;
}

int bypass_from_last_read(bpctl_dev_t *pbpctl_dev)
{
	uint32_t ctrl_ext = 0;
	bpctl_dev_t *pbpctl_dev_b = NULL;

	if ((pbpctl_dev->bp_caps & SW_CTL_CAP)
	    && (pbpctl_dev_b = get_status_port_fn(pbpctl_dev))) {
		ctrl_ext = BPCTL_READ_REG(pbpctl_dev_b, CTRL_EXT);
		BPCTL_BP_WRITE_REG(pbpctl_dev_b, CTRL_EXT,
				   (ctrl_ext & ~BPCTLI_CTRL_EXT_SDP7_DIR));
		ctrl_ext = BPCTL_READ_REG(pbpctl_dev_b, CTRL_EXT);
		if (ctrl_ext & BPCTLI_CTRL_EXT_SDP7_DATA)
			return 0;
		return 1;
	} else
		return BP_NOT_CAP;
}

int bypass_status_clear(bpctl_dev_t *pbpctl_dev)
{
	bpctl_dev_t *pbpctl_dev_b = NULL;

	if ((pbpctl_dev->bp_caps & SW_CTL_CAP)
	    && (pbpctl_dev_b = get_status_port_fn(pbpctl_dev))) {

		send_bypass_clear_pulse(pbpctl_dev_b, 1);
		return 0;
	} else
		return BP_NOT_CAP;
}

int bypass_flag_status(bpctl_dev_t *pbpctl_dev)
{

	if ((pbpctl_dev->bp_caps & BP_CAP)) {
		if (pbpctl_dev->bp_ext_ver >= PXG2BPI_VER) {
			return ((((read_reg(pbpctl_dev, STATUS_REG_ADDR)) &
				  BYPASS_FLAG_MASK) ==
				 BYPASS_FLAG_MASK) ? 1 : 0);
		}
	}
	return BP_NOT_CAP;
}

int bypass_flag_status_clear(bpctl_dev_t *pbpctl_dev)
{

	if (pbpctl_dev->bp_caps & BP_CAP) {
		if (pbpctl_dev->bp_ext_ver >= PXG2BPI_VER) {
			uint32_t status_reg = 0;
			status_reg = read_reg(pbpctl_dev, STATUS_REG_ADDR);
			write_reg(pbpctl_dev, status_reg & ~BYPASS_FLAG_MASK,
				  STATUS_REG_ADDR);
			return 0;
		}
	}
	return BP_NOT_CAP;
}

int bypass_change_status(bpctl_dev_t *pbpctl_dev)
{
	int ret = BP_NOT_CAP;

	if (pbpctl_dev->bp_caps & BP_STATUS_CHANGE_CAP) {
		if (pbpctl_dev->bp_ext_ver >= 0x8) {
			ret = bypass_flag_status(pbpctl_dev);
			bypass_flag_status_clear(pbpctl_dev);
		} else if (pbpctl_dev->bp_ext_ver >= PXG2BPI_VER) {
			ret = bypass_flag_status(pbpctl_dev);
			bypass_flag_status_clear(pbpctl_dev);
		} else {
			ret = bypass_from_last_read(pbpctl_dev);
			bypass_status_clear(pbpctl_dev);
		}
	}
	return ret;
}

int bypass_off_status(bpctl_dev_t *pbpctl_dev)
{

	if (pbpctl_dev->bp_caps & BP_CAP) {
		if (pbpctl_dev->bp_ext_ver >= PXG2BPI_VER) {
			return ((((read_reg(pbpctl_dev, STATUS_REG_ADDR)) &
				  BYPASS_OFF_MASK) == BYPASS_OFF_MASK) ? 1 : 0);
		}
	}
	return BP_NOT_CAP;
}

static int bypass_status(bpctl_dev_t *pbpctl_dev)
{
	u32 ctrl_ext = 0;
	if (pbpctl_dev->bp_caps & BP_CAP) {

		bpctl_dev_t *pbpctl_dev_b = NULL;

		if (!(pbpctl_dev_b = get_status_port_fn(pbpctl_dev)))
			return BP_NOT_CAP;

		if (INTEL_IF_SERIES(pbpctl_dev->subdevice)) {

			if (!pbpctl_dev->bp_status_un)
				return (((BPCTL_READ_REG
					  (pbpctl_dev_b,
					   CTRL_EXT)) &
					 BPCTLI_CTRL_EXT_SDP7_DATA) !=
					0 ? 1 : 0);
			else
				return BP_NOT_CAP;
		}
		if (pbpctl_dev->bp_ext_ver >= 0x8) {

			if (pbpctl_dev->bp_10g9) {
				ctrl_ext = BP10G_READ_REG(pbpctl_dev_b, I2CCTL);
				BP10G_WRITE_REG(pbpctl_dev_b, I2CCTL,
						(ctrl_ext | BP10G_I2C_CLK_OUT));
				return ((BP10G_READ_REG(pbpctl_dev_b, I2CCTL) &
					 BP10G_I2C_CLK_IN) != 0 ? 0 : 1);

			} else if (pbpctl_dev->bp_540) {
				return (((BP10G_READ_REG(pbpctl_dev_b, ESDP)) &
					 BP10G_SDP0_DATA) != 0 ? 0 : 1);
			}

			else if ((pbpctl_dev->bp_fiber5)
				 || (pbpctl_dev->bp_i80)) {
				return (((BPCTL_READ_REG(pbpctl_dev_b, CTRL)) &
					 BPCTLI_CTRL_SWDPIN0) != 0 ? 0 : 1);
			} else if (pbpctl_dev->bp_10gb) {
				ctrl_ext =
				    BP10GB_READ_REG(pbpctl_dev, MISC_REG_GPIO);
				BP10GB_WRITE_REG(pbpctl_dev, MISC_REG_GPIO,
						 (ctrl_ext | BP10GB_GPIO3_OE_P0)
						 & ~(BP10GB_GPIO3_SET_P0 |
						     BP10GB_GPIO3_CLR_P0));

				return (((BP10GB_READ_REG
					  (pbpctl_dev,
					   MISC_REG_GPIO)) & BP10GB_GPIO3_P0) !=
					0 ? 0 : 1);
			}

			else if (!pbpctl_dev->bp_10g)
				return (((BPCTL_READ_REG
					  (pbpctl_dev_b,
					   CTRL_EXT)) &
					 BPCTLI_CTRL_EXT_SDP7_DATA) !=
					0 ? 0 : 1);

			else {
				ctrl_ext = BP10G_READ_REG(pbpctl_dev_b, EODSDP);
				BP10G_WRITE_REG(pbpctl_dev_b, EODSDP,
						(ctrl_ext |
						 BP10G_SDP7_DATA_OUT));
				return ((BP10G_READ_REG(pbpctl_dev_b, EODSDP) &
					 BP10G_SDP7_DATA_IN) != 0 ? 0 : 1);
			}

		} else if (pbpctl_dev->media_type == bp_copper) {

			return (((BPCTL_READ_REG(pbpctl_dev_b, CTRL)) &
				 BPCTLI_CTRL_SWDPIN1) != 0 ? 1 : 0);
		} else {
			if ((bypass_status_clear(pbpctl_dev)) >= 0)
				return bypass_from_last_read(pbpctl_dev);
		}

	}
	return BP_NOT_CAP;
}

int default_pwron_status(bpctl_dev_t *pbpctl_dev)
{

	if (pbpctl_dev->bp_caps & SW_CTL_CAP) {
		if (pbpctl_dev->bp_caps & BP_PWUP_CTL_CAP) {
			if (pbpctl_dev->bp_ext_ver >= PXG2BPI_VER) {
				return ((((read_reg
					   (pbpctl_dev,
					    STATUS_REG_ADDR)) & DFLT_PWRON_MASK)
					 == DFLT_PWRON_MASK) ? 0 : 1);
			}
		}		/*else if ((!pbpctl_dev->bp_caps&BP_DIS_CAP)&&
				   (pbpctl_dev->bp_caps&BP_PWUP_ON_CAP))
				   return 1; */
	}
	return BP_NOT_CAP;
}

static int default_pwroff_status(bpctl_dev_t *pbpctl_dev)
{

	/*if ((!pbpctl_dev->bp_caps&BP_DIS_CAP)&&
	   (pbpctl_dev->bp_caps&BP_PWOFF_ON_CAP))
	   return 1; */
	if ((pbpctl_dev->bp_caps & SW_CTL_CAP)
	    && (pbpctl_dev->bp_caps & BP_PWOFF_CTL_CAP)) {
		return ((((read_reg(pbpctl_dev, STATUS_REG_ADDR)) &
			  DFLT_PWROFF_MASK) == DFLT_PWROFF_MASK) ? 0 : 1);
	}
	return BP_NOT_CAP;
}

int dis_bypass_cap_status(bpctl_dev_t *pbpctl_dev)
{

	if (pbpctl_dev->bp_caps & BP_DIS_CAP) {
		if (pbpctl_dev->bp_ext_ver >= PXG2BPI_VER) {
			return ((((read_reg(pbpctl_dev, STATUS_REG_ADDR)) &
				  DIS_BYPASS_CAP_MASK) ==
				 DIS_BYPASS_CAP_MASK) ? 1 : 0);
		}
	}
	return BP_NOT_CAP;
}

int cmd_en_status(bpctl_dev_t *pbpctl_dev)
{

	if (pbpctl_dev->bp_caps & SW_CTL_CAP) {
		if (pbpctl_dev->bp_ext_ver >= PXG2BPI_VER) {
			return ((((read_reg(pbpctl_dev, STATUS_REG_ADDR)) &
				  CMND_EN_MASK) == CMND_EN_MASK) ? 1 : 0);
		}
	}
	return BP_NOT_CAP;
}

int wdt_en_status(bpctl_dev_t *pbpctl_dev)
{

	if (pbpctl_dev->bp_caps & WD_CTL_CAP) {
		if (pbpctl_dev->bp_ext_ver >= PXG2BPI_VER) {
			return ((((read_reg(pbpctl_dev, STATUS_REG_ADDR)) &
				  WDT_EN_MASK) == WDT_EN_MASK) ? 1 : 0);
		}
	}
	return BP_NOT_CAP;
}

int wdt_programmed(bpctl_dev_t *pbpctl_dev, int *timeout)
{
	int ret = 0;
	if (pbpctl_dev->bp_caps & WD_CTL_CAP) {
		if (pbpctl_dev->bp_ext_ver >= PXG2BPI_VER) {
			if ((read_reg(pbpctl_dev, STATUS_REG_ADDR)) &
			    WDT_EN_MASK) {
				u8 wdt_val;
				wdt_val = read_reg(pbpctl_dev, WDT_REG_ADDR);
				*timeout = (1 << wdt_val) * 100;
			} else
				*timeout = 0;
		} else {
			int curr_wdt_status = pbpctl_dev->wdt_status;
			if (curr_wdt_status == WDT_STATUS_UNKNOWN)
				*timeout = -1;
			else
				*timeout =
				    curr_wdt_status ==
				    0 ? 0 : pbpctl_dev->bypass_timer_interval;
		};
	} else
		ret = BP_NOT_CAP;
	return ret;
}

int bypass_support(bpctl_dev_t *pbpctl_dev)
{
	int ret = 0;

	if (pbpctl_dev->bp_caps & SW_CTL_CAP) {
		if (pbpctl_dev->bp_ext_ver >= PXG2TBPI_VER) {
			ret =
			    ((((read_reg(pbpctl_dev, PRODUCT_CAP_REG_ADDR)) &
			       BYPASS_SUPPORT_MASK) ==
			      BYPASS_SUPPORT_MASK) ? 1 : 0);
		} else if (pbpctl_dev->bp_ext_ver == PXG2BPI_VER)
			ret = 1;
	} else
		ret = BP_NOT_CAP;
	return ret;
}

int tap_support(bpctl_dev_t *pbpctl_dev)
{
	int ret = 0;

	if (pbpctl_dev->bp_caps & SW_CTL_CAP) {
		if (pbpctl_dev->bp_ext_ver >= PXG2TBPI_VER) {
			ret =
			    ((((read_reg(pbpctl_dev, PRODUCT_CAP_REG_ADDR)) &
			       TAP_SUPPORT_MASK) == TAP_SUPPORT_MASK) ? 1 : 0);
		} else if (pbpctl_dev->bp_ext_ver == PXG2BPI_VER)
			ret = 0;
	} else
		ret = BP_NOT_CAP;
	return ret;
}

int normal_support(bpctl_dev_t *pbpctl_dev)
{
	int ret = BP_NOT_CAP;

	if (pbpctl_dev->bp_caps & SW_CTL_CAP) {
		if (pbpctl_dev->bp_ext_ver >= PXG2TBPI_VER) {
			ret =
			    ((((read_reg(pbpctl_dev, PRODUCT_CAP_REG_ADDR)) &
			       NORMAL_UNSUPPORT_MASK) ==
			      NORMAL_UNSUPPORT_MASK) ? 0 : 1);
		} else
			ret = 1;
	};
	return ret;
}

int get_bp_prod_caps(bpctl_dev_t *pbpctl_dev)
{
	if ((pbpctl_dev->bp_caps & SW_CTL_CAP) &&
	    (pbpctl_dev->bp_ext_ver >= PXG2TBPI_VER))
		return read_reg(pbpctl_dev, PRODUCT_CAP_REG_ADDR);
	return BP_NOT_CAP;

}

int tap_flag_status(bpctl_dev_t *pbpctl_dev)
{

	if (pbpctl_dev->bp_caps & TAP_STATUS_CAP) {
		if (pbpctl_dev->bp_ext_ver >= PXG2TBPI_VER)
			return ((((read_reg(pbpctl_dev, STATUS_TAP_REG_ADDR)) &
				  TAP_FLAG_MASK) == TAP_FLAG_MASK) ? 1 : 0);

	}
	return BP_NOT_CAP;
}

int tap_flag_status_clear(bpctl_dev_t *pbpctl_dev)
{
	uint32_t status_reg = 0;
	if (pbpctl_dev->bp_caps & TAP_STATUS_CAP) {
		if (pbpctl_dev->bp_ext_ver >= PXG2TBPI_VER) {
			status_reg = read_reg(pbpctl_dev, STATUS_TAP_REG_ADDR);
			write_reg(pbpctl_dev, status_reg & ~TAP_FLAG_MASK,
				  STATUS_TAP_REG_ADDR);
			return 0;
		}
	}
	return BP_NOT_CAP;
}

int tap_change_status(bpctl_dev_t *pbpctl_dev)
{
	int ret = BP_NOT_CAP;
	if (pbpctl_dev->bp_ext_ver >= PXG2TBPI_VER) {
		if (pbpctl_dev->bp_caps & TAP_CAP) {
			if (pbpctl_dev->bp_caps & BP_CAP) {
				ret = tap_flag_status(pbpctl_dev);
				tap_flag_status_clear(pbpctl_dev);
			} else {
				ret = bypass_from_last_read(pbpctl_dev);
				bypass_status_clear(pbpctl_dev);
			}
		}
	}
	return ret;
}

int tap_off_status(bpctl_dev_t *pbpctl_dev)
{
	if (pbpctl_dev->bp_caps & TAP_CAP) {
		if (pbpctl_dev->bp_ext_ver >= PXG2TBPI_VER)
			return ((((read_reg(pbpctl_dev, STATUS_TAP_REG_ADDR)) &
				  TAP_OFF_MASK) == TAP_OFF_MASK) ? 1 : 0);
	}
	return BP_NOT_CAP;
}

int tap_status(bpctl_dev_t *pbpctl_dev)
{
	u32 ctrl_ext = 0;

	if (pbpctl_dev->bp_caps & TAP_CAP) {
		bpctl_dev_t *pbpctl_dev_b = NULL;

		if (!(pbpctl_dev_b = get_status_port_fn(pbpctl_dev)))
			return BP_NOT_CAP;

		if (pbpctl_dev->bp_ext_ver >= 0x8) {
			if (!pbpctl_dev->bp_10g)
				return (((BPCTL_READ_REG
					  (pbpctl_dev_b,
					   CTRL_EXT)) &
					 BPCTLI_CTRL_EXT_SDP6_DATA) !=
					0 ? 0 : 1);
			else {
				ctrl_ext = BP10G_READ_REG(pbpctl_dev_b, EODSDP);
				BP10G_WRITE_REG(pbpctl_dev_b, EODSDP,
						(ctrl_ext |
						 BP10G_SDP6_DATA_OUT));
				return ((BP10G_READ_REG(pbpctl_dev_b, EODSDP) &
					 BP10G_SDP6_DATA_IN) != 0 ? 0 : 1);
			}

		} else if (pbpctl_dev->media_type == bp_copper)
			return (((BPCTL_READ_REG(pbpctl_dev, CTRL)) &
				 BPCTLI_CTRL_SWDPIN0) != 0 ? 1 : 0);
		else {
			if ((bypass_status_clear(pbpctl_dev)) >= 0)
				return bypass_from_last_read(pbpctl_dev);
		}

	}
	return BP_NOT_CAP;
}

int default_pwron_tap_status(bpctl_dev_t *pbpctl_dev)
{
	if (pbpctl_dev->bp_caps & TAP_PWUP_CTL_CAP) {
		if (pbpctl_dev->bp_ext_ver >= PXG2TBPI_VER)
			return ((((read_reg(pbpctl_dev, STATUS_TAP_REG_ADDR)) &
				  DFLT_PWRON_TAP_MASK) ==
				 DFLT_PWRON_TAP_MASK) ? 1 : 0);
	}
	return BP_NOT_CAP;
}

int dis_tap_cap_status(bpctl_dev_t *pbpctl_dev)
{
	if (pbpctl_dev->bp_caps & TAP_PWUP_CTL_CAP) {
		if (pbpctl_dev->bp_ext_ver >= PXG2TBPI_VER)
			return ((((read_reg(pbpctl_dev, STATUS_TAP_REG_ADDR)) &
				  DIS_TAP_CAP_MASK) ==
				 DIS_TAP_CAP_MASK) ? 1 : 0);
	}
	return BP_NOT_CAP;
}

int disc_flag_status(bpctl_dev_t *pbpctl_dev)
{

	if (pbpctl_dev->bp_caps & DISC_CAP) {
		if (pbpctl_dev->bp_ext_ver >= 0x8)
			return ((((read_reg(pbpctl_dev, STATUS_DISC_REG_ADDR)) &
				  DISC_FLAG_MASK) == DISC_FLAG_MASK) ? 1 : 0);

	}
	return BP_NOT_CAP;
}

int disc_flag_status_clear(bpctl_dev_t *pbpctl_dev)
{
	uint32_t status_reg = 0;
	if (pbpctl_dev->bp_caps & DISC_CAP) {
		if (pbpctl_dev->bp_ext_ver >= 0x8) {
			status_reg = read_reg(pbpctl_dev, STATUS_DISC_REG_ADDR);
			write_reg(pbpctl_dev, status_reg & ~DISC_FLAG_MASK,
				  STATUS_DISC_REG_ADDR);
			return BP_OK;
		}
	}
	return BP_NOT_CAP;
}

int disc_change_status(bpctl_dev_t *pbpctl_dev)
{
	int ret = BP_NOT_CAP;
	if (pbpctl_dev->bp_caps & DISC_CAP) {
		ret = disc_flag_status(pbpctl_dev);
		disc_flag_status_clear(pbpctl_dev);
		return ret;
	}
	return BP_NOT_CAP;
}

int disc_off_status(bpctl_dev_t *pbpctl_dev)
{
	bpctl_dev_t *pbpctl_dev_b = NULL;
	u32 ctrl_ext = 0;

	if (pbpctl_dev->bp_caps & DISC_CAP) {
		if (!(pbpctl_dev_b = get_status_port_fn(pbpctl_dev)))
			return BP_NOT_CAP;
		if (DISCF_IF_SERIES(pbpctl_dev->subdevice))
			return ((((read_reg(pbpctl_dev, STATUS_DISC_REG_ADDR)) &
				  DISC_OFF_MASK) == DISC_OFF_MASK) ? 1 : 0);

		if (pbpctl_dev->bp_i80) {
			return (((BPCTL_READ_REG(pbpctl_dev_b, CTRL_EXT)) &
				 BPCTLI_CTRL_EXT_SDP6_DATA) != 0 ? 1 : 0);

		}
		if (pbpctl_dev->bp_540) {
			ctrl_ext = BP10G_READ_REG(pbpctl_dev_b, ESDP);
			return ((BP10G_READ_REG(pbpctl_dev_b, ESDP) &
				 BP10G_SDP2_DATA) != 0 ? 1 : 0);

		}
		if (pbpctl_dev->media_type == bp_copper) {

#if 0
			return ((((read_reg(pbpctl_dev, STATUS_DISC_REG_ADDR)) &
				  DISC_OFF_MASK) == DISC_OFF_MASK) ? 1 : 0);
#endif
			if (!pbpctl_dev->bp_10g)
				return (((BPCTL_READ_REG(pbpctl_dev_b, CTRL)) &
					 BPCTLI_CTRL_SWDPIN1) != 0 ? 1 : 0);
			else
				return ((BP10G_READ_REG(pbpctl_dev_b, ESDP) &
					 BP10G_SDP1_DATA) != 0 ? 1 : 0);

		} else {

			if (pbpctl_dev->bp_10g9) {
				ctrl_ext = BP10G_READ_REG(pbpctl_dev_b, I2CCTL);
				BP10G_WRITE_REG(pbpctl_dev_b, I2CCTL,
						(ctrl_ext |
						 BP10G_I2C_DATA_OUT));
				return ((BP10G_READ_REG(pbpctl_dev_b, I2CCTL) &
					 BP10G_I2C_DATA_IN) != 0 ? 1 : 0);

			} else if (pbpctl_dev->bp_fiber5) {
				return (((BPCTL_READ_REG(pbpctl_dev_b, CTRL)) &
					 BPCTLI_CTRL_SWDPIN1) != 0 ? 1 : 0);
			} else if (pbpctl_dev->bp_10gb) {
				ctrl_ext =
				    BP10GB_READ_REG(pbpctl_dev, MISC_REG_GPIO);
				BP10GB_WRITE_REG(pbpctl_dev, MISC_REG_GPIO,
						 (ctrl_ext | BP10GB_GPIO3_OE_P1)
						 & ~(BP10GB_GPIO3_SET_P1 |
						     BP10GB_GPIO3_CLR_P1));

				return (((BP10GB_READ_REG
					  (pbpctl_dev,
					   MISC_REG_GPIO)) & BP10GB_GPIO3_P1) !=
					0 ? 1 : 0);
			}
			if (!pbpctl_dev->bp_10g) {

				return (((BPCTL_READ_REG
					  (pbpctl_dev_b,
					   CTRL_EXT)) &
					 BPCTLI_CTRL_EXT_SDP6_DATA) !=
					0 ? 1 : 0);
			} else {
				ctrl_ext = BP10G_READ_REG(pbpctl_dev_b, EODSDP);
				BP10G_WRITE_REG(pbpctl_dev_b, EODSDP,
						(ctrl_ext |
						 BP10G_SDP6_DATA_OUT));
				return (((BP10G_READ_REG(pbpctl_dev_b, EODSDP))
					 & BP10G_SDP6_DATA_IN) != 0 ? 1 : 0);
			}

		}
	}
	return BP_NOT_CAP;
}

static int disc_status(bpctl_dev_t *pbpctl_dev)
{
	int ctrl = 0;
	if (pbpctl_dev->bp_caps & DISC_CAP) {

		if ((ctrl = disc_off_status(pbpctl_dev)) < 0)
			return ctrl;
		return ((ctrl == 0) ? 1 : 0);

	}
	return BP_NOT_CAP;
}

int default_pwron_disc_status(bpctl_dev_t *pbpctl_dev)
{
	if (pbpctl_dev->bp_caps & DISC_PWUP_CTL_CAP) {
		if (pbpctl_dev->bp_ext_ver >= 0x8)
			return ((((read_reg(pbpctl_dev, STATUS_DISC_REG_ADDR)) &
				  DFLT_PWRON_DISC_MASK) ==
				 DFLT_PWRON_DISC_MASK) ? 1 : 0);
	}
	return BP_NOT_CAP;
}

int dis_disc_cap_status(bpctl_dev_t *pbpctl_dev)
{
	if (pbpctl_dev->bp_caps & DIS_DISC_CAP) {
		if (pbpctl_dev->bp_ext_ver >= 0x8)
			return ((((read_reg(pbpctl_dev, STATUS_DISC_REG_ADDR)) &
				  DIS_DISC_CAP_MASK) ==
				 DIS_DISC_CAP_MASK) ? 1 : 0);
	}
	return BP_NOT_CAP;
}

int disc_port_status(bpctl_dev_t *pbpctl_dev)
{
	int ret = BP_NOT_CAP;
	bpctl_dev_t *pbpctl_dev_m;

	if ((is_bypass_fn(pbpctl_dev)) == 1)
		pbpctl_dev_m = pbpctl_dev;
	else
		pbpctl_dev_m = get_master_port_fn(pbpctl_dev);
	if (pbpctl_dev_m == NULL)
		return BP_NOT_CAP;

	if (pbpctl_dev_m->bp_caps_ex & DISC_PORT_CAP_EX) {
		if (is_bypass_fn(pbpctl_dev) == 1) {
			return ((((read_reg(pbpctl_dev, STATUS_TAP_REG_ADDR)) &
				  TX_DISA_MASK) == TX_DISA_MASK) ? 1 : 0);
		} else
			return ((((read_reg(pbpctl_dev, STATUS_TAP_REG_ADDR)) &
				  TX_DISB_MASK) == TX_DISB_MASK) ? 1 : 0);

	}
	return ret;
}

int default_pwron_disc_port_status(bpctl_dev_t *pbpctl_dev)
{
	int ret = BP_NOT_CAP;
	bpctl_dev_t *pbpctl_dev_m;

	if ((is_bypass_fn(pbpctl_dev)) == 1)
		pbpctl_dev_m = pbpctl_dev;
	else
		pbpctl_dev_m = get_master_port_fn(pbpctl_dev);
	if (pbpctl_dev_m == NULL)
		return BP_NOT_CAP;

	if (pbpctl_dev_m->bp_caps_ex & DISC_PORT_CAP_EX) {
		if (is_bypass_fn(pbpctl_dev) == 1)
			return ret;
		/*  return((((read_reg(pbpctl_dev,STATUS_TAP_REG_ADDR)) & TX_DISA_MASK)==TX_DISA_MASK)?1:0); */
		else
			return ret;
		/*   return((((read_reg(pbpctl_dev,STATUS_TAP_REG_ADDR)) & TX_DISA_MASK)==TX_DISA_MASK)?1:0); */

	}
	return ret;
}

int wdt_exp_mode_status(bpctl_dev_t *pbpctl_dev)
{
	if (pbpctl_dev->bp_caps & WD_CTL_CAP) {
		if (pbpctl_dev->bp_ext_ver <= PXG2BPI_VER)
			return 0;	/* bypass mode */
		else if (pbpctl_dev->bp_ext_ver == PXG2TBPI_VER)
			return 1;	/* tap mode */
		else if (pbpctl_dev->bp_ext_ver >= PXE2TBPI_VER) {
			if (pbpctl_dev->bp_ext_ver >= 0x8) {
				if (((read_reg
				      (pbpctl_dev,
				       STATUS_DISC_REG_ADDR)) &
				     WDTE_DISC_BPN_MASK) == WDTE_DISC_BPN_MASK)
					return 2;
			}
			return ((((read_reg(pbpctl_dev, STATUS_TAP_REG_ADDR)) &
				  WDTE_TAP_BPN_MASK) ==
				 WDTE_TAP_BPN_MASK) ? 1 : 0);
		}
	}
	return BP_NOT_CAP;
}

int tpl2_flag_status(bpctl_dev_t *pbpctl_dev)
{

	if (pbpctl_dev->bp_caps_ex & TPL2_CAP_EX) {
		return ((((read_reg(pbpctl_dev, STATUS_DISC_REG_ADDR)) &
			  TPL2_FLAG_MASK) == TPL2_FLAG_MASK) ? 1 : 0);

	}
	return BP_NOT_CAP;
}

int tpl_hw_status(bpctl_dev_t *pbpctl_dev)
{
	bpctl_dev_t *pbpctl_dev_b = NULL;

	if (!(pbpctl_dev_b = get_status_port_fn(pbpctl_dev)))
		return BP_NOT_CAP;

	if (TPL_IF_SERIES(pbpctl_dev->subdevice))
		return (((BPCTL_READ_REG(pbpctl_dev, CTRL)) &
			 BPCTLI_CTRL_SWDPIN0) != 0 ? 1 : 0);
	return BP_NOT_CAP;
}


int bp_wait_at_pwup_status(bpctl_dev_t *pbpctl_dev)
{
	if (pbpctl_dev->bp_caps & SW_CTL_CAP) {
		if (pbpctl_dev->bp_ext_ver >= 0x8)
			return ((((read_reg(pbpctl_dev, CONT_CONFIG_REG_ADDR)) &
				  WAIT_AT_PWUP_MASK) ==
				 WAIT_AT_PWUP_MASK) ? 1 : 0);
	}
	return BP_NOT_CAP;
}

int bp_hw_reset_status(bpctl_dev_t *pbpctl_dev)
{

	if (pbpctl_dev->bp_caps & SW_CTL_CAP) {

		if (pbpctl_dev->bp_ext_ver >= 0x8)
			return ((((read_reg(pbpctl_dev, CONT_CONFIG_REG_ADDR)) &
				  EN_HW_RESET_MASK) ==
				 EN_HW_RESET_MASK) ? 1 : 0);
	}
	return BP_NOT_CAP;
}


int std_nic_status(bpctl_dev_t *pbpctl_dev)
{
	int status_val = 0;

	if (pbpctl_dev->bp_caps & STD_NIC_CAP) {
		if (INTEL_IF_SERIES(pbpctl_dev->subdevice))
			return BP_NOT_CAP;
		if (pbpctl_dev->bp_ext_ver >= BP_FW_EXT_VER8) {
			return ((((read_reg(pbpctl_dev, STATUS_DISC_REG_ADDR)) &
				  STD_NIC_ON_MASK) == STD_NIC_ON_MASK) ? 1 : 0);
		}

		if (pbpctl_dev->bp_ext_ver >= PXG2BPI_VER) {
			if (pbpctl_dev->bp_caps & BP_CAP) {
				status_val =
				    read_reg(pbpctl_dev, STATUS_REG_ADDR);
				if (((!(status_val & WDT_EN_MASK))
				     && ((status_val & STD_NIC_MASK) ==
					 STD_NIC_MASK)))
					status_val = 1;
				else
					return 0;
			}
			if (pbpctl_dev->bp_caps & TAP_CAP) {
				status_val =
				    read_reg(pbpctl_dev, STATUS_TAP_REG_ADDR);
				if ((status_val & STD_NIC_TAP_MASK) ==
				    STD_NIC_TAP_MASK)
					status_val = 1;
				else
					return 0;
			}
			if (pbpctl_dev->bp_caps & TAP_CAP) {
				if ((disc_off_status(pbpctl_dev)))
					status_val = 1;
				else
					return 0;
			}

			return status_val;
		}
	}
	return BP_NOT_CAP;
}

/******************************************************/
/**************SW_INIT*********************************/
/******************************************************/
void bypass_caps_init(bpctl_dev_t *pbpctl_dev)
{
	u_int32_t ctrl_ext = 0;
	bpctl_dev_t *pbpctl_dev_m = NULL;

#ifdef BYPASS_DEBUG
	int ret = 0;
	if (!(INTEL_IF_SERIES(adapter->bp_device_block.subdevice))) {
		ret = read_reg(pbpctl_dev, VER_REG_ADDR);
		printk("VER_REG reg1=%x\n", ret);
		ret = read_reg(pbpctl_dev, PRODUCT_CAP_REG_ADDR);
		printk("PRODUCT_CAP reg=%x\n", ret);
		ret = read_reg(pbpctl_dev, STATUS_TAP_REG_ADDR);
		printk("STATUS_TAP reg1=%x\n", ret);
		ret = read_reg(pbpctl_dev, 0x7);
		printk("SIG_REG reg1=%x\n", ret);
		ret = read_reg(pbpctl_dev, STATUS_REG_ADDR);
		printk("STATUS_REG_ADDR=%x\n", ret);
		ret = read_reg(pbpctl_dev, WDT_REG_ADDR);
		printk("WDT_REG_ADDR=%x\n", ret);
		ret = read_reg(pbpctl_dev, TMRL_REG_ADDR);
		printk("TMRL_REG_ADDR=%x\n", ret);
		ret = read_reg(pbpctl_dev, TMRH_REG_ADDR);
		printk("TMRH_REG_ADDR=%x\n", ret);
	}
#endif
	if ((pbpctl_dev->bp_fiber5) || (pbpctl_dev->bp_10g9)) {
		pbpctl_dev->media_type = bp_fiber;
	} else if (pbpctl_dev->bp_10gb) {
		if (BP10GB_CX4_SERIES(pbpctl_dev->subdevice))
			pbpctl_dev->media_type = bp_cx4;
		else
			pbpctl_dev->media_type = bp_fiber;

	}

	else if (pbpctl_dev->bp_540)
		pbpctl_dev->media_type = bp_none;
	else if (!pbpctl_dev->bp_10g) {

		ctrl_ext = BPCTL_READ_REG(pbpctl_dev, CTRL_EXT);
		if ((ctrl_ext & BPCTLI_CTRL_EXT_LINK_MODE_MASK) == 0x0)
			pbpctl_dev->media_type = bp_copper;
		else
			pbpctl_dev->media_type = bp_fiber;

	} else {
		if (BP10G_CX4_SERIES(pbpctl_dev->subdevice))
			pbpctl_dev->media_type = bp_cx4;
		else
			pbpctl_dev->media_type = bp_fiber;
	}

	if (is_bypass_fn(pbpctl_dev)) {

		pbpctl_dev->bp_caps |= BP_PWOFF_ON_CAP;
		if (pbpctl_dev->media_type == bp_fiber)
			pbpctl_dev->bp_caps |=
			    (TX_CTL_CAP | TX_STATUS_CAP | TPL_CAP);

		if (TPL_IF_SERIES(pbpctl_dev->subdevice)) {
			pbpctl_dev->bp_caps |= TPL_CAP;
		}

		if (INTEL_IF_SERIES(pbpctl_dev->subdevice)) {
			pbpctl_dev->bp_caps |=
			    (BP_CAP | BP_STATUS_CAP | SW_CTL_CAP |
			     BP_PWUP_ON_CAP | BP_PWUP_OFF_CAP | BP_PWOFF_OFF_CAP
			     | WD_CTL_CAP | WD_STATUS_CAP | STD_NIC_CAP |
			     WD_TIMEOUT_CAP);

			pbpctl_dev->bp_ext_ver = OLD_IF_VER;
			return;
		}

		if ((pbpctl_dev->bp_fw_ver == 0xff) &&
		    OLD_IF_SERIES(pbpctl_dev->subdevice)) {

			pbpctl_dev->bp_caps |=
			    (BP_CAP | BP_STATUS_CAP | BP_STATUS_CHANGE_CAP |
			     SW_CTL_CAP | BP_PWUP_ON_CAP | WD_CTL_CAP |
			     WD_STATUS_CAP | WD_TIMEOUT_CAP);

			pbpctl_dev->bp_ext_ver = OLD_IF_VER;
			return;
		}

		else {
			switch (pbpctl_dev->bp_fw_ver) {
			case BP_FW_VER_A0:
			case BP_FW_VER_A1:{
					pbpctl_dev->bp_ext_ver =
					    (pbpctl_dev->
					     bp_fw_ver & EXT_VER_MASK);
					break;
				}
			default:{
					if ((bypass_sign_check(pbpctl_dev)) !=
					    1) {
						pbpctl_dev->bp_caps = 0;
						return;
					}
					pbpctl_dev->bp_ext_ver =
					    (pbpctl_dev->
					     bp_fw_ver & EXT_VER_MASK);
				}
			}
		}

		if (pbpctl_dev->bp_ext_ver == PXG2BPI_VER)
			pbpctl_dev->bp_caps |=
			    (BP_CAP | BP_STATUS_CAP | BP_STATUS_CHANGE_CAP |
			     SW_CTL_CAP | BP_DIS_CAP | BP_DIS_STATUS_CAP |
			     BP_PWUP_ON_CAP | BP_PWUP_OFF_CAP | BP_PWUP_CTL_CAP
			     | WD_CTL_CAP | STD_NIC_CAP | WD_STATUS_CAP |
			     WD_TIMEOUT_CAP);
		else if (pbpctl_dev->bp_ext_ver >= PXG2TBPI_VER) {
			int cap_reg;

			pbpctl_dev->bp_caps |=
			    (SW_CTL_CAP | WD_CTL_CAP | WD_STATUS_CAP |
			     WD_TIMEOUT_CAP);
			cap_reg = get_bp_prod_caps(pbpctl_dev);

			if ((cap_reg & NORMAL_UNSUPPORT_MASK) ==
			    NORMAL_UNSUPPORT_MASK)
				pbpctl_dev->bp_caps |= NIC_CAP_NEG;
			else
				pbpctl_dev->bp_caps |= STD_NIC_CAP;

			if ((normal_support(pbpctl_dev)) == 1)

				pbpctl_dev->bp_caps |= STD_NIC_CAP;

			else
				pbpctl_dev->bp_caps |= NIC_CAP_NEG;
			if ((cap_reg & BYPASS_SUPPORT_MASK) ==
			    BYPASS_SUPPORT_MASK) {
				pbpctl_dev->bp_caps |=
				    (BP_CAP | BP_STATUS_CAP |
				     BP_STATUS_CHANGE_CAP | BP_DIS_CAP |
				     BP_DIS_STATUS_CAP | BP_PWUP_ON_CAP |
				     BP_PWUP_OFF_CAP | BP_PWUP_CTL_CAP);
				if (pbpctl_dev->bp_ext_ver >= BP_FW_EXT_VER7)
					pbpctl_dev->bp_caps |=
					    BP_PWOFF_ON_CAP | BP_PWOFF_OFF_CAP |
					    BP_PWOFF_CTL_CAP;
			}
			if ((cap_reg & TAP_SUPPORT_MASK) == TAP_SUPPORT_MASK) {
				pbpctl_dev->bp_caps |=
				    (TAP_CAP | TAP_STATUS_CAP |
				     TAP_STATUS_CHANGE_CAP | TAP_DIS_CAP |
				     TAP_DIS_STATUS_CAP | TAP_PWUP_ON_CAP |
				     TAP_PWUP_OFF_CAP | TAP_PWUP_CTL_CAP);
			}
			if (pbpctl_dev->bp_ext_ver >= BP_FW_EXT_VER8) {
				if ((cap_reg & DISC_SUPPORT_MASK) ==
				    DISC_SUPPORT_MASK)
					pbpctl_dev->bp_caps |=
					    (DISC_CAP | DISC_DIS_CAP |
					     DISC_PWUP_CTL_CAP);
				if ((cap_reg & TPL2_SUPPORT_MASK) ==
				    TPL2_SUPPORT_MASK) {
					pbpctl_dev->bp_caps_ex |= TPL2_CAP_EX;
					pbpctl_dev->bp_caps |= TPL_CAP;
					pbpctl_dev->bp_tpl_flag =
					    tpl2_flag_status(pbpctl_dev);
				}

			}

			if (pbpctl_dev->bp_ext_ver >= BP_FW_EXT_VER9) {
				if ((cap_reg & DISC_PORT_SUPPORT_MASK) ==
				    DISC_PORT_SUPPORT_MASK) {
					pbpctl_dev->bp_caps_ex |=
					    DISC_PORT_CAP_EX;
					pbpctl_dev->bp_caps |=
					    (TX_CTL_CAP | TX_STATUS_CAP);
				}

			}

		}
		if (pbpctl_dev->bp_ext_ver >= PXG2BPI_VER) {
			if ((read_reg(pbpctl_dev, STATUS_REG_ADDR)) &
			    WDT_EN_MASK)
				pbpctl_dev->wdt_status = WDT_STATUS_EN;
			else
				pbpctl_dev->wdt_status = WDT_STATUS_DIS;
		}

	} else if ((P2BPFI_IF_SERIES(pbpctl_dev->subdevice)) ||
		   (PEGF5_IF_SERIES(pbpctl_dev->subdevice)) ||
		   (PEGF80_IF_SERIES(pbpctl_dev->subdevice)) ||
		   (BP10G9_IF_SERIES(pbpctl_dev->subdevice))) {
		pbpctl_dev->bp_caps |= (TX_CTL_CAP | TX_STATUS_CAP);
	}
	if ((pbpctl_dev->subdevice & 0xa00) == 0xa00)
		pbpctl_dev->bp_caps |= (TX_CTL_CAP | TX_STATUS_CAP);
	if (PEG5_IF_SERIES(pbpctl_dev->subdevice))
		pbpctl_dev->bp_caps |= (TX_CTL_CAP | TX_STATUS_CAP);

	if (BP10GB_IF_SERIES(pbpctl_dev->subdevice)) {
		pbpctl_dev->bp_caps &= ~(TX_CTL_CAP | TX_STATUS_CAP);
	}
	pbpctl_dev_m = get_master_port_fn(pbpctl_dev);
	if (pbpctl_dev_m != NULL) {
		int cap_reg = 0;
		if (pbpctl_dev_m->bp_ext_ver >= 0x9) {
			cap_reg = get_bp_prod_caps(pbpctl_dev_m);
			if ((cap_reg & DISC_PORT_SUPPORT_MASK) ==
			    DISC_PORT_SUPPORT_MASK)
				pbpctl_dev->bp_caps |=
				    (TX_CTL_CAP | TX_STATUS_CAP);
			pbpctl_dev->bp_caps_ex |= DISC_PORT_CAP_EX;
		}
	}
}

int bypass_off_init(bpctl_dev_t *pbpctl_dev)
{
	int ret = 0;

	if ((ret = cmnd_on(pbpctl_dev)) < 0)
		return ret;
	if (INTEL_IF_SERIES(pbpctl_dev->subdevice))
		return dis_bypass_cap(pbpctl_dev);
	wdt_off(pbpctl_dev);
	if (pbpctl_dev->bp_caps & BP_CAP)
		bypass_off(pbpctl_dev);
	if (pbpctl_dev->bp_caps & TAP_CAP)
		tap_off(pbpctl_dev);
	cmnd_off(pbpctl_dev);
	return 0;
}

void remove_bypass_wd_auto(bpctl_dev_t *pbpctl_dev)
{
#ifdef BP_SELF_TEST
	bpctl_dev_t *pbpctl_dev_sl = NULL;
#endif

	if (pbpctl_dev->bp_caps & WD_CTL_CAP) {

		del_timer_sync(&pbpctl_dev->bp_timer);
#ifdef BP_SELF_TEST
		pbpctl_dev_sl = get_status_port_fn(pbpctl_dev);
		if (pbpctl_dev_sl && (pbpctl_dev_sl->ndev)) {
			if ((pbpctl_dev_sl->ndev->netdev_ops)
			    && (pbpctl_dev_sl->old_ops)) {
				rtnl_lock();
				pbpctl_dev_sl->ndev->netdev_ops =
				    pbpctl_dev_sl->old_ops;
				pbpctl_dev_sl->old_ops = NULL;

				rtnl_unlock();

			}

		}
#endif
	}

}

int init_bypass_wd_auto(bpctl_dev_t *pbpctl_dev)
{
	if (pbpctl_dev->bp_caps & WD_CTL_CAP) {
		init_timer(&pbpctl_dev->bp_timer);
		pbpctl_dev->bp_timer.function = &wd_reset_timer;
		pbpctl_dev->bp_timer.data = (unsigned long)pbpctl_dev;
		return 1;
	}
	return BP_NOT_CAP;
}

#ifdef BP_SELF_TEST
int bp_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	bpctl_dev_t *pbpctl_dev = NULL, *pbpctl_dev_m = NULL;
	int idx_dev = 0;
	struct ethhdr *eth = (struct ethhdr *)skb->data;

	for (idx_dev = 0;
	     ((bpctl_dev_arr[idx_dev].ndev != NULL) && (idx_dev < device_num));
	     idx_dev++) {
		if (bpctl_dev_arr[idx_dev].ndev == dev) {
			pbpctl_dev = &bpctl_dev_arr[idx_dev];
			break;
		}
	}
	if (!pbpctl_dev)
		return 1;
	if ((htons(ETH_P_BPTEST) == eth->h_proto)) {

		pbpctl_dev_m = get_master_port_fn(pbpctl_dev);
		if (pbpctl_dev_m) {

			if (bypass_status(pbpctl_dev_m)) {
				cmnd_on(pbpctl_dev_m);
				bypass_off(pbpctl_dev_m);
				cmnd_off(pbpctl_dev_m);
			}
			wdt_timer_reload(pbpctl_dev_m);
		}
		dev_kfree_skb_irq(skb);
		return 0;
	}
	return pbpctl_dev->hard_start_xmit_save(skb, dev);
}
#endif

int set_bypass_wd_auto(bpctl_dev_t *pbpctl_dev, unsigned int param)
{
	if (pbpctl_dev->bp_caps & WD_CTL_CAP) {
		if (pbpctl_dev->reset_time != param) {
			if (INTEL_IF_SERIES(pbpctl_dev->subdevice))
				pbpctl_dev->reset_time =
				    (param <
				     WDT_AUTO_MIN_INT) ? WDT_AUTO_MIN_INT :
				    param;
			else
				pbpctl_dev->reset_time = param;
			if (param)
				mod_timer(&pbpctl_dev->bp_timer, jiffies);
		}
		return 0;
	}
	return BP_NOT_CAP;
}

int get_bypass_wd_auto(bpctl_dev_t *pbpctl_dev)
{

	if (pbpctl_dev->bp_caps & WD_CTL_CAP) {
		return pbpctl_dev->reset_time;
	}
	return BP_NOT_CAP;
}

#ifdef  BP_SELF_TEST

int set_bp_self_test(bpctl_dev_t *pbpctl_dev, unsigned int param)
{
	bpctl_dev_t *pbpctl_dev_sl = NULL;

	if (pbpctl_dev->bp_caps & WD_CTL_CAP) {
		pbpctl_dev->bp_self_test_flag = param == 0 ? 0 : 1;
		pbpctl_dev_sl = get_status_port_fn(pbpctl_dev);

		if ((pbpctl_dev_sl->ndev) && (pbpctl_dev_sl->ndev->netdev_ops)) {
			rtnl_lock();
			if (pbpctl_dev->bp_self_test_flag == 1) {

				pbpctl_dev_sl->old_ops =
				    pbpctl_dev_sl->ndev->netdev_ops;
				pbpctl_dev_sl->new_ops =
				    *pbpctl_dev_sl->old_ops;
				pbpctl_dev_sl->new_ops.ndo_start_xmit =
				    bp_hard_start_xmit;
				pbpctl_dev_sl->ndev->netdev_ops =
				    &pbpctl_dev_sl->new_ops;

			} else if (pbpctl_dev_sl->old_ops) {
				pbpctl_dev_sl->ndev->netdev_ops =
				    pbpctl_dev_sl->old_ops;
				pbpctl_dev_sl->old_ops = NULL;
			}
			rtnl_unlock();
		}

		set_bypass_wd_auto(pbpctl_dev, param);
		return 0;
	}
	return BP_NOT_CAP;
}

int get_bp_self_test(bpctl_dev_t *pbpctl_dev)
{

	if (pbpctl_dev->bp_caps & WD_CTL_CAP) {
		if (pbpctl_dev->bp_self_test_flag == 1)
			return pbpctl_dev->reset_time;
		else
			return 0;
	}
	return BP_NOT_CAP;
}

#endif

/**************************************************************/
/************************* API ********************************/
/**************************************************************/

int is_bypass_fn(bpctl_dev_t *pbpctl_dev)
{
	if (!pbpctl_dev)
		return -1;

	return (((pbpctl_dev->func == 0) || (pbpctl_dev->func == 2)) ? 1 : 0);
}

int set_bypass_fn(bpctl_dev_t *pbpctl_dev, int bypass_mode)
{
	int ret = 0;

	if (!(pbpctl_dev->bp_caps & BP_CAP))
		return BP_NOT_CAP;
	if ((ret = cmnd_on(pbpctl_dev)) < 0)
		return ret;
	if (!bypass_mode)
		ret = bypass_off(pbpctl_dev);
	else
		ret = bypass_on(pbpctl_dev);
	cmnd_off(pbpctl_dev);

	return ret;
}

int get_bypass_fn(bpctl_dev_t *pbpctl_dev)
{
	return bypass_status(pbpctl_dev);
}

int get_bypass_change_fn(bpctl_dev_t *pbpctl_dev)
{
	if (!pbpctl_dev)
		return -1;

	return bypass_change_status(pbpctl_dev);
}

int set_dis_bypass_fn(bpctl_dev_t *pbpctl_dev, int dis_param)
{
	int ret = 0;
	if (!pbpctl_dev)
		return -1;

	if (!(pbpctl_dev->bp_caps & BP_DIS_CAP))
		return BP_NOT_CAP;
	if ((ret = cmnd_on(pbpctl_dev)) < 0)
		return ret;
	if (dis_param)
		ret = dis_bypass_cap(pbpctl_dev);
	else
		ret = en_bypass_cap(pbpctl_dev);
	cmnd_off(pbpctl_dev);
	return ret;
}

int get_dis_bypass_fn(bpctl_dev_t *pbpctl_dev)
{
	if (!pbpctl_dev)
		return -1;

	return dis_bypass_cap_status(pbpctl_dev);
}

int set_bypass_pwoff_fn(bpctl_dev_t *pbpctl_dev, int bypass_mode)
{
	int ret = 0;
	if (!pbpctl_dev)
		return -1;

	if (!(pbpctl_dev->bp_caps & BP_PWOFF_CTL_CAP))
		return BP_NOT_CAP;
	if ((ret = cmnd_on(pbpctl_dev)) < 0)
		return ret;
	if (bypass_mode)
		ret = bypass_state_pwroff(pbpctl_dev);
	else
		ret = normal_state_pwroff(pbpctl_dev);
	cmnd_off(pbpctl_dev);
	return ret;
}

int get_bypass_pwoff_fn(bpctl_dev_t *pbpctl_dev)
{
	if (!pbpctl_dev)
		return -1;

	return default_pwroff_status(pbpctl_dev);
}

int set_bypass_pwup_fn(bpctl_dev_t *pbpctl_dev, int bypass_mode)
{
	int ret = 0;
	if (!pbpctl_dev)
		return -1;

	if (!(pbpctl_dev->bp_caps & BP_PWUP_CTL_CAP))
		return BP_NOT_CAP;
	if ((ret = cmnd_on(pbpctl_dev)) < 0)
		return ret;
	if (bypass_mode)
		ret = bypass_state_pwron(pbpctl_dev);
	else
		ret = normal_state_pwron(pbpctl_dev);
	cmnd_off(pbpctl_dev);
	return ret;
}

int get_bypass_pwup_fn(bpctl_dev_t *pbpctl_dev)
{
	if (!pbpctl_dev)
		return -1;

	return default_pwron_status(pbpctl_dev);
}

int set_bypass_wd_fn(bpctl_dev_t *pbpctl_dev, int timeout)
{
	int ret = 0;
	if (!pbpctl_dev)
		return -1;

	if (!(pbpctl_dev->bp_caps & WD_CTL_CAP))
		return BP_NOT_CAP;

	if ((ret = cmnd_on(pbpctl_dev)) < 0)
		return ret;
	if (!timeout)
		ret = wdt_off(pbpctl_dev);
	else {
		wdt_on(pbpctl_dev, timeout);
		ret = pbpctl_dev->bypass_timer_interval;
	}
	cmnd_off(pbpctl_dev);
	return ret;
}

int get_bypass_wd_fn(bpctl_dev_t *pbpctl_dev, int *timeout)
{
	if (!pbpctl_dev)
		return -1;

	return wdt_programmed(pbpctl_dev, timeout);
}

int get_wd_expire_time_fn(bpctl_dev_t *pbpctl_dev, int *time_left)
{
	if (!pbpctl_dev)
		return -1;

	return wdt_timer(pbpctl_dev, time_left);
}

int reset_bypass_wd_timer_fn(bpctl_dev_t *pbpctl_dev)
{
	if (!pbpctl_dev)
		return -1;

	return wdt_timer_reload(pbpctl_dev);
}

int get_wd_set_caps_fn(bpctl_dev_t *pbpctl_dev)
{
	int bp_status = 0;

	unsigned int step_value = TIMEOUT_MAX_STEP + 1, bit_cnt = 0;
	if (!pbpctl_dev)
		return -1;

	if (INTEL_IF_SERIES(pbpctl_dev->subdevice))
		return BP_NOT_CAP;

	while ((step_value >>= 1))
		bit_cnt++;

	if (is_bypass_fn(pbpctl_dev)) {
		bp_status =
		    WD_STEP_COUNT_MASK(bit_cnt) | WDT_STEP_TIME |
		    WD_MIN_TIME_MASK(TIMEOUT_UNIT / 100);
	} else
		return -1;

	return bp_status;
}

int set_std_nic_fn(bpctl_dev_t *pbpctl_dev, int nic_mode)
{
	int ret = 0;
	if (!pbpctl_dev)
		return -1;

	if (!(pbpctl_dev->bp_caps & STD_NIC_CAP))
		return BP_NOT_CAP;

	if ((ret = cmnd_on(pbpctl_dev)) < 0)
		return ret;
	if (nic_mode)
		ret = std_nic_on(pbpctl_dev);
	else
		ret = std_nic_off(pbpctl_dev);
	cmnd_off(pbpctl_dev);
	return ret;
}

int get_std_nic_fn(bpctl_dev_t *pbpctl_dev)
{
	if (!pbpctl_dev)
		return -1;

	return std_nic_status(pbpctl_dev);
}

int set_tap_fn(bpctl_dev_t *pbpctl_dev, int tap_mode)
{
	if (!pbpctl_dev)
		return -1;

	if ((pbpctl_dev->bp_caps & TAP_CAP) && ((cmnd_on(pbpctl_dev)) >= 0)) {
		if (!tap_mode)
			tap_off(pbpctl_dev);
		else
			tap_on(pbpctl_dev);
		cmnd_off(pbpctl_dev);
		return 0;
	}
	return BP_NOT_CAP;
}

int get_tap_fn(bpctl_dev_t *pbpctl_dev)
{
	if (!pbpctl_dev)
		return -1;

	return tap_status(pbpctl_dev);
}

int set_tap_pwup_fn(bpctl_dev_t *pbpctl_dev, int tap_mode)
{
	int ret = 0;
	if (!pbpctl_dev)
		return -1;

	if ((pbpctl_dev->bp_caps & TAP_PWUP_CTL_CAP)
	    && ((cmnd_on(pbpctl_dev)) >= 0)) {
		if (tap_mode)
			ret = tap_state_pwron(pbpctl_dev);
		else
			ret = normal_state_pwron(pbpctl_dev);
		cmnd_off(pbpctl_dev);
	} else
		ret = BP_NOT_CAP;
	return ret;
}

int get_tap_pwup_fn(bpctl_dev_t *pbpctl_dev)
{
	int ret = 0;
	if (!pbpctl_dev)
		return -1;

	if ((ret = default_pwron_tap_status(pbpctl_dev)) < 0)
		return ret;
	return ((ret == 0) ? 1 : 0);
}

int get_tap_change_fn(bpctl_dev_t *pbpctl_dev)
{
	if (!pbpctl_dev)
		return -1;

	return tap_change_status(pbpctl_dev);
}

int set_dis_tap_fn(bpctl_dev_t *pbpctl_dev, int dis_param)
{
	int ret = 0;
	if (!pbpctl_dev)
		return -1;

	if ((pbpctl_dev->bp_caps & TAP_DIS_CAP) && ((cmnd_on(pbpctl_dev)) >= 0)) {
		if (dis_param)
			ret = dis_tap_cap(pbpctl_dev);
		else
			ret = en_tap_cap(pbpctl_dev);
		cmnd_off(pbpctl_dev);
		return ret;
	} else
		return BP_NOT_CAP;
}

int get_dis_tap_fn(bpctl_dev_t *pbpctl_dev)
{
	if (!pbpctl_dev)
		return -1;

	return dis_tap_cap_status(pbpctl_dev);
}

int set_disc_fn(bpctl_dev_t *pbpctl_dev, int disc_mode)
{
	if (!pbpctl_dev)
		return -1;

	if ((pbpctl_dev->bp_caps & DISC_CAP) && ((cmnd_on(pbpctl_dev)) >= 0)) {
		if (!disc_mode)
			disc_off(pbpctl_dev);
		else
			disc_on(pbpctl_dev);
		cmnd_off(pbpctl_dev);

		return BP_OK;
	}
	return BP_NOT_CAP;
}

int get_disc_fn(bpctl_dev_t *pbpctl_dev)
{
	int ret = 0;
	if (!pbpctl_dev)
		return -1;

	ret = disc_status(pbpctl_dev);

	return ret;
}

int set_disc_pwup_fn(bpctl_dev_t *pbpctl_dev, int disc_mode)
{
	int ret = 0;
	if (!pbpctl_dev)
		return -1;

	if ((pbpctl_dev->bp_caps & DISC_PWUP_CTL_CAP)
	    && ((cmnd_on(pbpctl_dev)) >= 0)) {
		if (disc_mode)
			ret = disc_state_pwron(pbpctl_dev);
		else
			ret = normal_state_pwron(pbpctl_dev);
		cmnd_off(pbpctl_dev);
	} else
		ret = BP_NOT_CAP;
	return ret;
}

int get_disc_pwup_fn(bpctl_dev_t *pbpctl_dev)
{
	int ret = 0;
	if (!pbpctl_dev)
		return -1;

	ret = default_pwron_disc_status(pbpctl_dev);
	return (ret == 0 ? 1 : (ret < 0 ? BP_NOT_CAP : 0));
}

int get_disc_change_fn(bpctl_dev_t *pbpctl_dev)
{
	int ret = 0;
	if (!pbpctl_dev)
		return -1;

	ret = disc_change_status(pbpctl_dev);
	return ret;
}

int set_dis_disc_fn(bpctl_dev_t *pbpctl_dev, int dis_param)
{
	int ret = 0;
	if (!pbpctl_dev)
		return -1;

	if ((pbpctl_dev->bp_caps & DISC_DIS_CAP)
	    && ((cmnd_on(pbpctl_dev)) >= 0)) {
		if (dis_param)
			ret = dis_disc_cap(pbpctl_dev);
		else
			ret = en_disc_cap(pbpctl_dev);
		cmnd_off(pbpctl_dev);
		return ret;
	} else
		return BP_NOT_CAP;
}

int get_dis_disc_fn(bpctl_dev_t *pbpctl_dev)
{
	int ret = 0;
	if (!pbpctl_dev)
		return -1;

	ret = dis_disc_cap_status(pbpctl_dev);

	return ret;
}

int set_disc_port_fn(bpctl_dev_t *pbpctl_dev, int disc_mode)
{
	int ret = BP_NOT_CAP;
	if (!pbpctl_dev)
		return -1;

	if (!disc_mode)
		ret = disc_port_off(pbpctl_dev);
	else
		ret = disc_port_on(pbpctl_dev);

	return ret;
}

int get_disc_port_fn(bpctl_dev_t *pbpctl_dev)
{
	if (!pbpctl_dev)
		return -1;

	return disc_port_status(pbpctl_dev);
}

int set_disc_port_pwup_fn(bpctl_dev_t *pbpctl_dev, int disc_mode)
{
	int ret = BP_NOT_CAP;
	if (!pbpctl_dev)
		return -1;

	if (!disc_mode)
		ret = normal_port_state_pwron(pbpctl_dev);
	else
		ret = disc_port_state_pwron(pbpctl_dev);

	return ret;
}

int get_disc_port_pwup_fn(bpctl_dev_t *pbpctl_dev)
{
	int ret = 0;
	if (!pbpctl_dev)
		return -1;

	if ((ret = default_pwron_disc_port_status(pbpctl_dev)) < 0)
		return ret;
	return ((ret == 0) ? 1 : 0);
}

int get_wd_exp_mode_fn(bpctl_dev_t *pbpctl_dev)
{
	if (!pbpctl_dev)
		return -1;

	return wdt_exp_mode_status(pbpctl_dev);
}

int set_wd_exp_mode_fn(bpctl_dev_t *pbpctl_dev, int param)
{
	if (!pbpctl_dev)
		return -1;

	return wdt_exp_mode(pbpctl_dev, param);
}

int reset_cont_fn(bpctl_dev_t *pbpctl_dev)
{
	int ret = 0;
	if (!pbpctl_dev)
		return -1;

	if ((ret = cmnd_on(pbpctl_dev)) < 0)
		return ret;
	return reset_cont(pbpctl_dev);
}

int set_tx_fn(bpctl_dev_t *pbpctl_dev, int tx_state)
{

	bpctl_dev_t *pbpctl_dev_b = NULL;
	if (!pbpctl_dev)
		return -1;

	if ((pbpctl_dev->bp_caps & TPL_CAP) &&
	    (pbpctl_dev->bp_caps & SW_CTL_CAP)) {
		if ((pbpctl_dev->bp_tpl_flag))
			return BP_NOT_CAP;
	} else if ((pbpctl_dev_b = get_master_port_fn(pbpctl_dev))) {
		if ((pbpctl_dev_b->bp_caps & TPL_CAP) &&
		    (pbpctl_dev_b->bp_tpl_flag))
			return BP_NOT_CAP;
	}
	return set_tx(pbpctl_dev, tx_state);
}

int set_bp_force_link_fn(int dev_num, int tx_state)
{
	static bpctl_dev_t *bpctl_dev_curr;

	if ((dev_num < 0) || (dev_num > device_num)
	    || (bpctl_dev_arr[dev_num].pdev == NULL))
		return -1;
	bpctl_dev_curr = &bpctl_dev_arr[dev_num];

	return set_bp_force_link(bpctl_dev_curr, tx_state);
}

int set_wd_autoreset_fn(bpctl_dev_t *pbpctl_dev, int param)
{
	if (!pbpctl_dev)
		return -1;

	return set_bypass_wd_auto(pbpctl_dev, param);
}

int get_wd_autoreset_fn(bpctl_dev_t *pbpctl_dev)
{
	if (!pbpctl_dev)
		return -1;

	return get_bypass_wd_auto(pbpctl_dev);
}

#ifdef BP_SELF_TEST
int set_bp_self_test_fn(bpctl_dev_t *pbpctl_dev, int param)
{
	if (!pbpctl_dev)
		return -1;

	return set_bp_self_test(pbpctl_dev, param);
}

int get_bp_self_test_fn(bpctl_dev_t *pbpctl_dev)
{
	if (!pbpctl_dev)
		return -1;

	return get_bp_self_test(pbpctl_dev);
}

#endif

int get_bypass_caps_fn(bpctl_dev_t *pbpctl_dev)
{
	if (!pbpctl_dev)
		return -1;

	return pbpctl_dev->bp_caps;

}

int get_bypass_slave_fn(bpctl_dev_t *pbpctl_dev, bpctl_dev_t **pbpctl_dev_out)
{
	int idx_dev = 0;
	if (!pbpctl_dev)
		return -1;

	if ((pbpctl_dev->func == 0) || (pbpctl_dev->func == 2)) {
		for (idx_dev = 0;
		     ((bpctl_dev_arr[idx_dev].pdev != NULL)
		      && (idx_dev < device_num)); idx_dev++) {
			if ((bpctl_dev_arr[idx_dev].bus == pbpctl_dev->bus)
			    && (bpctl_dev_arr[idx_dev].slot ==
				pbpctl_dev->slot)) {
				if ((pbpctl_dev->func == 0)
				    && (bpctl_dev_arr[idx_dev].func == 1)) {
					*pbpctl_dev_out =
					    &bpctl_dev_arr[idx_dev];
					return 1;
				}
				if ((pbpctl_dev->func == 2) &&
				    (bpctl_dev_arr[idx_dev].func == 3)) {
					*pbpctl_dev_out =
					    &bpctl_dev_arr[idx_dev];
					return 1;
				}
			}
		}
		return -1;
	} else
		return 0;
}

int is_bypass(bpctl_dev_t *pbpctl_dev)
{
	if (!pbpctl_dev)
		return -1;

	if ((pbpctl_dev->func == 0) || (pbpctl_dev->func == 2))
		return 1;
	else
		return 0;
}

int get_tx_fn(bpctl_dev_t *pbpctl_dev)
{
	bpctl_dev_t *pbpctl_dev_b = NULL;
	if (!pbpctl_dev)
		return -1;

	if ((pbpctl_dev->bp_caps & TPL_CAP) &&
	    (pbpctl_dev->bp_caps & SW_CTL_CAP)) {
		if ((pbpctl_dev->bp_tpl_flag))
			return BP_NOT_CAP;
	} else if ((pbpctl_dev_b = get_master_port_fn(pbpctl_dev))) {
		if ((pbpctl_dev_b->bp_caps & TPL_CAP) &&
		    (pbpctl_dev_b->bp_tpl_flag))
			return BP_NOT_CAP;
	}
	return tx_status(pbpctl_dev);
}

int get_bp_force_link_fn(int dev_num)
{
	static bpctl_dev_t *bpctl_dev_curr;

	if ((dev_num < 0) || (dev_num > device_num)
	    || (bpctl_dev_arr[dev_num].pdev == NULL))
		return -1;
	bpctl_dev_curr = &bpctl_dev_arr[dev_num];

	return bp_force_link_status(bpctl_dev_curr);
}

static int get_bypass_link_status(bpctl_dev_t *pbpctl_dev)
{
	if (!pbpctl_dev)
		return -1;

	if (pbpctl_dev->media_type == bp_fiber)
		return ((BPCTL_READ_REG(pbpctl_dev, CTRL) &
			 BPCTLI_CTRL_SWDPIN1));
	else
		return ((BPCTL_READ_REG(pbpctl_dev, STATUS) &
			 BPCTLI_STATUS_LU));

}

static void bp_tpl_timer_fn(unsigned long param)
{
	bpctl_dev_t *pbpctl_dev = (bpctl_dev_t *) param;
	uint32_t link1, link2;
	bpctl_dev_t *pbpctl_dev_b = NULL;

	if (!(pbpctl_dev_b = get_status_port_fn(pbpctl_dev)))
		return;

	if (!pbpctl_dev->bp_tpl_flag) {
		set_tx(pbpctl_dev_b, 1);
		set_tx(pbpctl_dev, 1);
		return;
	}
	link1 = get_bypass_link_status(pbpctl_dev);

	link2 = get_bypass_link_status(pbpctl_dev_b);
	if ((link1) && (tx_status(pbpctl_dev))) {
		if ((!link2) && (tx_status(pbpctl_dev_b))) {
			set_tx(pbpctl_dev, 0);
		} else if (!tx_status(pbpctl_dev_b)) {
			set_tx(pbpctl_dev_b, 1);
		}
	} else if ((!link1) && (tx_status(pbpctl_dev))) {
		if ((link2) && (tx_status(pbpctl_dev_b))) {
			set_tx(pbpctl_dev_b, 0);
		}
	} else if ((link1) && (!tx_status(pbpctl_dev))) {
		if ((link2) && (tx_status(pbpctl_dev_b))) {
			set_tx(pbpctl_dev, 1);
		}
	} else if ((!link1) && (!tx_status(pbpctl_dev))) {
		if ((link2) && (tx_status(pbpctl_dev_b))) {
			set_tx(pbpctl_dev, 1);
		}
	}

	mod_timer(&pbpctl_dev->bp_tpl_timer, jiffies + BP_LINK_MON_DELAY * HZ);
}

void remove_bypass_tpl_auto(bpctl_dev_t *pbpctl_dev)
{
	bpctl_dev_t *pbpctl_dev_b = NULL;
	if (!pbpctl_dev)
		return;
	pbpctl_dev_b = get_status_port_fn(pbpctl_dev);

	if (pbpctl_dev->bp_caps & TPL_CAP) {
		del_timer_sync(&pbpctl_dev->bp_tpl_timer);
		pbpctl_dev->bp_tpl_flag = 0;
		pbpctl_dev_b = get_status_port_fn(pbpctl_dev);
		if (pbpctl_dev_b)
			set_tx(pbpctl_dev_b, 1);
		set_tx(pbpctl_dev, 1);
	}
	return;
}

int init_bypass_tpl_auto(bpctl_dev_t *pbpctl_dev)
{
	if (!pbpctl_dev)
		return -1;
	if (pbpctl_dev->bp_caps & TPL_CAP) {
		init_timer(&pbpctl_dev->bp_tpl_timer);
		pbpctl_dev->bp_tpl_timer.function = &bp_tpl_timer_fn;
		pbpctl_dev->bp_tpl_timer.data = (unsigned long)pbpctl_dev;
		return BP_OK;
	}
	return BP_NOT_CAP;
}

int set_bypass_tpl_auto(bpctl_dev_t *pbpctl_dev, unsigned int param)
{
	if (!pbpctl_dev)
		return -1;
	if (pbpctl_dev->bp_caps & TPL_CAP) {
		if ((param) && (!pbpctl_dev->bp_tpl_flag)) {
			pbpctl_dev->bp_tpl_flag = param;
			mod_timer(&pbpctl_dev->bp_tpl_timer, jiffies + 1);
			return BP_OK;
		};
		if ((!param) && (pbpctl_dev->bp_tpl_flag))
			remove_bypass_tpl_auto(pbpctl_dev);

		return BP_OK;
	}
	return BP_NOT_CAP;
}

int get_bypass_tpl_auto(bpctl_dev_t *pbpctl_dev)
{
	if (!pbpctl_dev)
		return -1;
	if (pbpctl_dev->bp_caps & TPL_CAP) {
		return pbpctl_dev->bp_tpl_flag;
	}
	return BP_NOT_CAP;
}

int set_tpl_fn(bpctl_dev_t *pbpctl_dev, int tpl_mode)
{

	bpctl_dev_t *pbpctl_dev_b = NULL;
	if (!pbpctl_dev)
		return -1;

	pbpctl_dev_b = get_status_port_fn(pbpctl_dev);

	if (pbpctl_dev->bp_caps & TPL_CAP) {
		if (tpl_mode) {
			if ((pbpctl_dev_b = get_status_port_fn(pbpctl_dev)))
				set_tx(pbpctl_dev_b, 1);
			set_tx(pbpctl_dev, 1);
		}
		if ((TPL_IF_SERIES(pbpctl_dev->subdevice)) ||
		    (pbpctl_dev->bp_caps_ex & TPL2_CAP_EX)) {
			pbpctl_dev->bp_tpl_flag = tpl_mode;
			if (!tpl_mode)
				tpl_hw_off(pbpctl_dev);
			else
				tpl_hw_on(pbpctl_dev);
		} else
			set_bypass_tpl_auto(pbpctl_dev, tpl_mode);
		return 0;
	}
	return BP_NOT_CAP;
}

int get_tpl_fn(bpctl_dev_t *pbpctl_dev)
{
	int ret = BP_NOT_CAP;
	if (!pbpctl_dev)
		return -1;

	if (pbpctl_dev->bp_caps & TPL_CAP) {
		if (pbpctl_dev->bp_caps_ex & TPL2_CAP_EX)
			return tpl2_flag_status(pbpctl_dev);
		ret = pbpctl_dev->bp_tpl_flag;
	}
	return ret;
}

int set_bp_wait_at_pwup_fn(bpctl_dev_t *pbpctl_dev, int tap_mode)
{
	if (!pbpctl_dev)
		return -1;

	if (pbpctl_dev->bp_caps & SW_CTL_CAP) {
		/* bp_lock(pbp_device_block); */
		cmnd_on(pbpctl_dev);
		if (!tap_mode)
			bp_wait_at_pwup_dis(pbpctl_dev);
		else
			bp_wait_at_pwup_en(pbpctl_dev);
		cmnd_off(pbpctl_dev);

		/* bp_unlock(pbp_device_block); */
		return BP_OK;
	}
	return BP_NOT_CAP;
}

int get_bp_wait_at_pwup_fn(bpctl_dev_t *pbpctl_dev)
{
	int ret = 0;
	if (!pbpctl_dev)
		return -1;

	/* bp_lock(pbp_device_block); */
	ret = bp_wait_at_pwup_status(pbpctl_dev);
	/* bp_unlock(pbp_device_block); */

	return ret;
}

int set_bp_hw_reset_fn(bpctl_dev_t *pbpctl_dev, int tap_mode)
{
	if (!pbpctl_dev)
		return -1;

	if (pbpctl_dev->bp_caps & SW_CTL_CAP) {
		/*   bp_lock(pbp_device_block); */
		cmnd_on(pbpctl_dev);

		if (!tap_mode)
			bp_hw_reset_dis(pbpctl_dev);
		else
			bp_hw_reset_en(pbpctl_dev);
		cmnd_off(pbpctl_dev);
		/*    bp_unlock(pbp_device_block); */
		return BP_OK;
	}
	return BP_NOT_CAP;
}

int get_bp_hw_reset_fn(bpctl_dev_t *pbpctl_dev)
{
	int ret = 0;
	if (!pbpctl_dev)
		return -1;

	/* bp_lock(pbp_device_block); */
	ret = bp_hw_reset_status(pbpctl_dev);

	/* bp_unlock(pbp_device_block); */

	return ret;
}


int get_bypass_info_fn(bpctl_dev_t *pbpctl_dev, char *dev_name,
		       char *add_param)
{
	if (!pbpctl_dev)
		return -1;
	if (!is_bypass_fn(pbpctl_dev))
		return -1;
	strcpy(dev_name, pbpctl_dev->name);
	*add_param = pbpctl_dev->bp_fw_ver;
	return 0;
}

int get_dev_idx_bsf(int bus, int slot, int func)
{
	int idx_dev = 0;
	for (idx_dev = 0;
	     ((bpctl_dev_arr[idx_dev].pdev != NULL) && (idx_dev < device_num));
	     idx_dev++) {
		if ((bus == bpctl_dev_arr[idx_dev].bus)
		    && (slot == bpctl_dev_arr[idx_dev].slot)
		    && (func == bpctl_dev_arr[idx_dev].func))

			return idx_dev;
	}
	return -1;
}

static void str_low(char *str)
{
	int i;

	for (i = 0; i < strlen(str); i++)
		if ((str[i] >= 65) && (str[i] <= 90))
			str[i] += 32;
}

static unsigned long str_to_hex(char *p)
{
	unsigned long hex = 0;
	unsigned long length = strlen(p), shift = 0;
	unsigned char dig = 0;

	str_low(p);
	length = strlen(p);

	if (length == 0)
		return 0;

	do {
		dig = p[--length];
		dig = dig < 'a' ? (dig - '0') : (dig - 'a' + 0xa);
		hex |= (dig << shift);
		shift += 4;
	} while (length);
	return hex;
}

static int get_dev_idx(int ifindex)
{
	int idx_dev = 0;

	for (idx_dev = 0;
	     ((bpctl_dev_arr[idx_dev].pdev != NULL) && (idx_dev < device_num));
	     idx_dev++) {
		if (ifindex == bpctl_dev_arr[idx_dev].ifindex)
			return idx_dev;
	}

	return -1;
}

static bpctl_dev_t *get_dev_idx_p(int ifindex)
{
	int idx_dev = 0;

	for (idx_dev = 0;
	     ((bpctl_dev_arr[idx_dev].pdev != NULL) && (idx_dev < device_num));
	     idx_dev++) {
		if (ifindex == bpctl_dev_arr[idx_dev].ifindex)
			return &bpctl_dev_arr[idx_dev];
	}

	return NULL;
}

static void if_scan_init(void)
{
	int idx_dev = 0;
	struct net_device *dev;
	int ifindex;
	/* rcu_read_lock(); */
	/* rtnl_lock();     */
	/* rcu_read_lock(); */

	for_each_netdev(&init_net, dev) {

		struct ethtool_drvinfo drvinfo;
		char cbuf[32];
		char *buf = NULL;
		char res[10];
		int i = 0;
		int bus = 0, slot = 0, func = 0;
		ifindex = dev->ifindex;

		memset(res, 0, 10);
		memset(&drvinfo, 0, sizeof(struct ethtool_drvinfo));

		if (dev->ethtool_ops && dev->ethtool_ops->get_drvinfo) {
			memset(&drvinfo, 0, sizeof(drvinfo));
			dev->ethtool_ops->get_drvinfo(dev, &drvinfo);
		} else
			continue;
		if (!strcmp(drvinfo.bus_info, "N/A"))
			continue;
		memcpy(&cbuf, drvinfo.bus_info, 32);
		buf = &cbuf[0];

		while (*buf++ != ':') ;
		for (i = 0; i < 10; i++, buf++) {
			if (*buf == ':')
				break;
			res[i] = *buf;

		}
		buf++;
		bus = str_to_hex(res);
		memset(res, 0, 10);

		for (i = 0; i < 10; i++, buf++) {
			if (*buf == '.')
				break;
			res[i] = *buf;

		}
		buf++;
		slot = str_to_hex(res);
		func = str_to_hex(buf);
		idx_dev = get_dev_idx_bsf(bus, slot, func);

		if (idx_dev != -1) {

			bpctl_dev_arr[idx_dev].ifindex = ifindex;
			bpctl_dev_arr[idx_dev].ndev = dev;

		}

	}
	/* rtnl_unlock();     */
	/* rcu_read_unlock(); */

}

static long device_ioctl(struct file *file,	/* see include/linux/fs.h */
			 unsigned int ioctl_num,	/* number and param for ioctl */
			 unsigned long ioctl_param)
{
	struct bpctl_cmd bpctl_cmd;
	int dev_idx = 0;
	bpctl_dev_t *pbpctl_dev_out;
	void __user *argp = (void __user *)ioctl_param;
	int ret = 0;
	unsigned long flags;

	static bpctl_dev_t *pbpctl_dev;

	/* lock_kernel(); */
	lock_bpctl();
	/* local_irq_save(flags); */
	/* if(!spin_trylock_irqsave(&bpvm_lock)){
	   local_irq_restore(flags);
	   unlock_bpctl();
	   unlock_kernel();
	   return -1;
	   } */
	/* spin_lock_irqsave(&bpvm_lock, flags); */

/*
* Switch according to the ioctl called
*/
	if (ioctl_num == IOCTL_TX_MSG(IF_SCAN)) {
		if_scan_init();
		ret = SUCCESS;
		goto bp_exit;
	}
	if (copy_from_user(&bpctl_cmd, argp, sizeof(struct bpctl_cmd))) {

		ret = -EFAULT;
		goto bp_exit;
	}

	if (ioctl_num == IOCTL_TX_MSG(GET_DEV_NUM)) {
		bpctl_cmd.out_param[0] = device_num;
		if (copy_to_user
		    (argp, (void *)&bpctl_cmd, sizeof(struct bpctl_cmd))) {
			ret = -EFAULT;
			goto bp_exit;
		}
		ret = SUCCESS;
		goto bp_exit;

	}
	/* lock_bpctl();      */
	/* preempt_disable(); */
	local_irq_save(flags);
	if (!spin_trylock(&bpvm_lock)) {
		local_irq_restore(flags);
		unlock_bpctl();
		return -1;
	}

/*    	preempt_disable();
	rcu_read_lock();
      	spin_lock_irqsave(&bpvm_lock, flags);
*/
	if ((bpctl_cmd.in_param[5]) ||
	    (bpctl_cmd.in_param[6]) || (bpctl_cmd.in_param[7]))
		dev_idx = get_dev_idx_bsf(bpctl_cmd.in_param[5],
					  bpctl_cmd.in_param[6],
					  bpctl_cmd.in_param[7]);
	else if (bpctl_cmd.in_param[1] == 0)
		dev_idx = bpctl_cmd.in_param[0];
	else
		dev_idx = get_dev_idx(bpctl_cmd.in_param[1]);

	if (dev_idx < 0 || dev_idx > device_num) {
		/* unlock_bpctl();
		   preempt_enable(); */
		ret = -EOPNOTSUPP;
		/* preempt_enable();
		   rcu_read_unlock();  */
		spin_unlock_irqrestore(&bpvm_lock, flags);
		goto bp_exit;
	}

	bpctl_cmd.out_param[0] = bpctl_dev_arr[dev_idx].bus;
	bpctl_cmd.out_param[1] = bpctl_dev_arr[dev_idx].slot;
	bpctl_cmd.out_param[2] = bpctl_dev_arr[dev_idx].func;
	bpctl_cmd.out_param[3] = bpctl_dev_arr[dev_idx].ifindex;

	if ((bpctl_dev_arr[dev_idx].bp_10gb)
	    && (!(bpctl_dev_arr[dev_idx].ifindex))) {
		printk("Please load network driver for %s adapter!\n",
		       bpctl_dev_arr[dev_idx].name);
		bpctl_cmd.status = -1;
		ret = SUCCESS;
		/* preempt_enable(); */
		/* rcu_read_unlock(); */
		spin_unlock_irqrestore(&bpvm_lock, flags);
		goto bp_exit;

	}
	if ((bpctl_dev_arr[dev_idx].bp_10gb) && (bpctl_dev_arr[dev_idx].ndev)) {
		if (!(bpctl_dev_arr[dev_idx].ndev->flags & IFF_UP)) {
			if (!(bpctl_dev_arr[dev_idx].ndev->flags & IFF_UP)) {
				printk
				    ("Please bring up network interfaces for %s adapter!\n",
				     bpctl_dev_arr[dev_idx].name);
				bpctl_cmd.status = -1;
				ret = SUCCESS;
				/* preempt_enable(); */
				/* rcu_read_unlock(); */
				spin_unlock_irqrestore(&bpvm_lock, flags);
				goto bp_exit;
			}

		}
	}

	if ((dev_idx < 0) || (dev_idx > device_num)
	    || (bpctl_dev_arr[dev_idx].pdev == NULL)) {
		bpctl_cmd.status = -1;
		goto bpcmd_exit;
	}

	pbpctl_dev = &bpctl_dev_arr[dev_idx];

	switch (ioctl_num) {
	case IOCTL_TX_MSG(SET_BYPASS_PWOFF):
		bpctl_cmd.status =
		    set_bypass_pwoff_fn(pbpctl_dev, bpctl_cmd.in_param[2]);
		break;

	case IOCTL_TX_MSG(GET_BYPASS_PWOFF):
		bpctl_cmd.status = get_bypass_pwoff_fn(pbpctl_dev);
		break;

	case IOCTL_TX_MSG(SET_BYPASS_PWUP):
		bpctl_cmd.status =
		    set_bypass_pwup_fn(pbpctl_dev, bpctl_cmd.in_param[2]);
		break;

	case IOCTL_TX_MSG(GET_BYPASS_PWUP):
		bpctl_cmd.status = get_bypass_pwup_fn(pbpctl_dev);
		break;

	case IOCTL_TX_MSG(SET_BYPASS_WD):
		bpctl_cmd.status =
		    set_bypass_wd_fn(pbpctl_dev, bpctl_cmd.in_param[2]);
		break;

	case IOCTL_TX_MSG(GET_BYPASS_WD):
		bpctl_cmd.status =
		    get_bypass_wd_fn(pbpctl_dev, (int *)&(bpctl_cmd.data[0]));
		break;

	case IOCTL_TX_MSG(GET_WD_EXPIRE_TIME):
		bpctl_cmd.status =
		    get_wd_expire_time_fn(pbpctl_dev,
					  (int *)&(bpctl_cmd.data[0]));
		break;

	case IOCTL_TX_MSG(RESET_BYPASS_WD_TIMER):
		bpctl_cmd.status = reset_bypass_wd_timer_fn(pbpctl_dev);
		break;

	case IOCTL_TX_MSG(GET_WD_SET_CAPS):
		bpctl_cmd.status = get_wd_set_caps_fn(pbpctl_dev);
		break;

	case IOCTL_TX_MSG(SET_STD_NIC):
		bpctl_cmd.status =
		    set_std_nic_fn(pbpctl_dev, bpctl_cmd.in_param[2]);
		break;

	case IOCTL_TX_MSG(GET_STD_NIC):
		bpctl_cmd.status = get_std_nic_fn(pbpctl_dev);
		break;

	case IOCTL_TX_MSG(SET_TAP):
		bpctl_cmd.status =
		    set_tap_fn(pbpctl_dev, bpctl_cmd.in_param[2]);
		break;

	case IOCTL_TX_MSG(GET_TAP):
		bpctl_cmd.status = get_tap_fn(pbpctl_dev);
		break;

	case IOCTL_TX_MSG(GET_TAP_CHANGE):
		bpctl_cmd.status = get_tap_change_fn(pbpctl_dev);
		break;

	case IOCTL_TX_MSG(SET_DIS_TAP):
		bpctl_cmd.status =
		    set_dis_tap_fn(pbpctl_dev, bpctl_cmd.in_param[2]);
		break;

	case IOCTL_TX_MSG(GET_DIS_TAP):
		bpctl_cmd.status = get_dis_tap_fn(pbpctl_dev);
		break;

	case IOCTL_TX_MSG(SET_TAP_PWUP):
		bpctl_cmd.status =
		    set_tap_pwup_fn(pbpctl_dev, bpctl_cmd.in_param[2]);
		break;

	case IOCTL_TX_MSG(GET_TAP_PWUP):
		bpctl_cmd.status = get_tap_pwup_fn(pbpctl_dev);
		break;

	case IOCTL_TX_MSG(SET_WD_EXP_MODE):
		bpctl_cmd.status =
		    set_wd_exp_mode_fn(pbpctl_dev, bpctl_cmd.in_param[2]);
		break;

	case IOCTL_TX_MSG(GET_WD_EXP_MODE):
		bpctl_cmd.status = get_wd_exp_mode_fn(pbpctl_dev);
		break;

	case IOCTL_TX_MSG(GET_DIS_BYPASS):
		bpctl_cmd.status = get_dis_bypass_fn(pbpctl_dev);
		break;

	case IOCTL_TX_MSG(SET_DIS_BYPASS):
		bpctl_cmd.status =
		    set_dis_bypass_fn(pbpctl_dev, bpctl_cmd.in_param[2]);
		break;

	case IOCTL_TX_MSG(GET_BYPASS_CHANGE):
		bpctl_cmd.status = get_bypass_change_fn(pbpctl_dev);
		break;

	case IOCTL_TX_MSG(GET_BYPASS):
		bpctl_cmd.status = get_bypass_fn(pbpctl_dev);
		break;

	case IOCTL_TX_MSG(SET_BYPASS):
		bpctl_cmd.status =
		    set_bypass_fn(pbpctl_dev, bpctl_cmd.in_param[2]);
		break;

	case IOCTL_TX_MSG(GET_BYPASS_CAPS):
		bpctl_cmd.status = get_bypass_caps_fn(pbpctl_dev);
		/*preempt_enable(); */
		/*rcu_read_unlock();*/
		spin_unlock_irqrestore(&bpvm_lock, flags);
		if (copy_to_user
		    (argp, (void *)&bpctl_cmd, sizeof(struct bpctl_cmd))) {
			/*unlock_bpctl();   */
			/*preempt_enable(); */
			ret = -EFAULT;
			goto bp_exit;
		}
		goto bp_exit;

	case IOCTL_TX_MSG(GET_BYPASS_SLAVE):
		bpctl_cmd.status =
		    get_bypass_slave_fn(pbpctl_dev, &pbpctl_dev_out);
		if (bpctl_cmd.status == 1) {
			bpctl_cmd.out_param[4] = pbpctl_dev_out->bus;
			bpctl_cmd.out_param[5] = pbpctl_dev_out->slot;
			bpctl_cmd.out_param[6] = pbpctl_dev_out->func;
			bpctl_cmd.out_param[7] = pbpctl_dev_out->ifindex;
		}
		break;

	case IOCTL_TX_MSG(IS_BYPASS):
		bpctl_cmd.status = is_bypass(pbpctl_dev);
		break;
	case IOCTL_TX_MSG(SET_TX):
		bpctl_cmd.status = set_tx_fn(pbpctl_dev, bpctl_cmd.in_param[2]);
		break;
	case IOCTL_TX_MSG(GET_TX):
		bpctl_cmd.status = get_tx_fn(pbpctl_dev);
		break;
	case IOCTL_TX_MSG(SET_WD_AUTORESET):
		bpctl_cmd.status =
		    set_wd_autoreset_fn(pbpctl_dev, bpctl_cmd.in_param[2]);

		break;
	case IOCTL_TX_MSG(GET_WD_AUTORESET):

		bpctl_cmd.status = get_wd_autoreset_fn(pbpctl_dev);
		break;
	case IOCTL_TX_MSG(SET_DISC):
		bpctl_cmd.status =
		    set_disc_fn(pbpctl_dev, bpctl_cmd.in_param[2]);
		break;
	case IOCTL_TX_MSG(GET_DISC):
		bpctl_cmd.status = get_disc_fn(pbpctl_dev);
		break;
	case IOCTL_TX_MSG(GET_DISC_CHANGE):
		bpctl_cmd.status = get_disc_change_fn(pbpctl_dev);
		break;
	case IOCTL_TX_MSG(SET_DIS_DISC):
		bpctl_cmd.status =
		    set_dis_disc_fn(pbpctl_dev, bpctl_cmd.in_param[2]);
		break;
	case IOCTL_TX_MSG(GET_DIS_DISC):
		bpctl_cmd.status = get_dis_disc_fn(pbpctl_dev);
		break;
	case IOCTL_TX_MSG(SET_DISC_PWUP):
		bpctl_cmd.status =
		    set_disc_pwup_fn(pbpctl_dev, bpctl_cmd.in_param[2]);
		break;
	case IOCTL_TX_MSG(GET_DISC_PWUP):
		bpctl_cmd.status = get_disc_pwup_fn(pbpctl_dev);
		break;

	case IOCTL_TX_MSG(GET_BYPASS_INFO):

		bpctl_cmd.status =
		    get_bypass_info_fn(pbpctl_dev, (char *)&bpctl_cmd.data,
				       (char *)&bpctl_cmd.out_param[4]);
		break;

	case IOCTL_TX_MSG(SET_TPL):
		bpctl_cmd.status =
		    set_tpl_fn(pbpctl_dev, bpctl_cmd.in_param[2]);
		break;

	case IOCTL_TX_MSG(GET_TPL):
		bpctl_cmd.status = get_tpl_fn(pbpctl_dev);
		break;
	case IOCTL_TX_MSG(SET_BP_WAIT_AT_PWUP):
		bpctl_cmd.status =
		    set_bp_wait_at_pwup_fn(pbpctl_dev, bpctl_cmd.in_param[2]);
		break;

	case IOCTL_TX_MSG(GET_BP_WAIT_AT_PWUP):
		bpctl_cmd.status = get_bp_wait_at_pwup_fn(pbpctl_dev);
		break;
	case IOCTL_TX_MSG(SET_BP_HW_RESET):
		bpctl_cmd.status =
		    set_bp_hw_reset_fn(pbpctl_dev, bpctl_cmd.in_param[2]);
		break;

	case IOCTL_TX_MSG(GET_BP_HW_RESET):
		bpctl_cmd.status = get_bp_hw_reset_fn(pbpctl_dev);
		break;
#ifdef BP_SELF_TEST
	case IOCTL_TX_MSG(SET_BP_SELF_TEST):
		bpctl_cmd.status =
		    set_bp_self_test_fn(pbpctl_dev, bpctl_cmd.in_param[2]);

		break;
	case IOCTL_TX_MSG(GET_BP_SELF_TEST):
		bpctl_cmd.status = get_bp_self_test_fn(pbpctl_dev);
		break;

#endif
#if 0
	case IOCTL_TX_MSG(SET_DISC_PORT):
		bpctl_cmd.status =
		    set_disc_port_fn(pbpctl_dev, bpctl_cmd.in_param[2]);
		break;

	case IOCTL_TX_MSG(GET_DISC_PORT):
		bpctl_cmd.status = get_disc_port_fn(pbpctl_dev);
		break;

	case IOCTL_TX_MSG(SET_DISC_PORT_PWUP):
		bpctl_cmd.status =
		    set_disc_port_pwup_fn(pbpctl_dev, bpctl_cmd.in_param[2]);
		break;

	case IOCTL_TX_MSG(GET_DISC_PORT_PWUP):
		bpctl_cmd.status = get_disc_port_pwup_fn(pbpctl_dev);
		break;
#endif
	case IOCTL_TX_MSG(SET_BP_FORCE_LINK):
		bpctl_cmd.status =
		    set_bp_force_link_fn(dev_idx, bpctl_cmd.in_param[2]);
		break;

	case IOCTL_TX_MSG(GET_BP_FORCE_LINK):
		bpctl_cmd.status = get_bp_force_link_fn(dev_idx);
		break;

	default:
		/*    unlock_bpctl(); */

		ret = -EOPNOTSUPP;
		/* preempt_enable(); */
		/* rcu_read_unlock();*/
		spin_unlock_irqrestore(&bpvm_lock, flags);
		goto bp_exit;
	}
	/* unlock_bpctl();   */
	/* preempt_enable(); */
 bpcmd_exit:
	/* rcu_read_unlock(); */
	spin_unlock_irqrestore(&bpvm_lock, flags);
	if (copy_to_user(argp, (void *)&bpctl_cmd, sizeof(struct bpctl_cmd)))
		ret = -EFAULT;
	ret = SUCCESS;
 bp_exit:
	/* unlock_kernel(); */
	/* spin_unlock_irqrestore(&bpvm_lock, flags); */
	unlock_bpctl();
	/* unlock_kernel(); */
	return ret;
}

struct file_operations Fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = device_ioctl,
	.open = device_open,
	.release = device_release,	/* a.k.a. close */
};

#ifndef PCI_DEVICE
#define PCI_DEVICE(vend,dev) \
	.vendor = (vend), .device = (dev), \
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID
#endif

#define SILICOM_E1000BP_ETHERNET_DEVICE(device_id) {\
	PCI_DEVICE(SILICOM_VID, device_id)}

typedef enum {
	PXG2BPFI,
	PXG2BPFIL,
	PXG2BPFILX,
	PXG2BPFILLX,
	PXGBPI,
	PXGBPIG,
	PXG2TBFI,
	PXG4BPI,
	PXG4BPFI,
	PEG4BPI,
	PEG2BPI,
	PEG4BPIN,
	PEG2BPFI,
	PEG2BPFILX,
	PMCXG2BPFI,
	PMCXG2BPFIN,
	PEG4BPII,
	PEG4BPFII,
	PXG4BPFILX,
	PMCXG2BPIN,
	PMCXG4BPIN,
	PXG2BISC1,
	PEG2TBFI,
	PXG2TBI,
	PXG4BPFID,
	PEG4BPFI,
	PEG4BPIPT,
	PXG6BPI,
	PEG4BPIL,
	PMCXG2BPIN2,
	PMCXG4BPIN2,
	PMCX2BPI,
	PEG2BPFID,
	PEG2BPFIDLX,
	PMCX4BPI,
	MEG2BPFILN,
	MEG2BPFINX,
	PEG4BPFILX,
	PE10G2BPISR,
	PE10G2BPILR,
	MHIO8AD,
	PE10G2BPICX4,
	PEG2BPI5,
	PEG6BPI,
	PEG4BPFI5,
	PEG4BPFI5LX,
	MEG2BPFILXLN,
	PEG2BPIX1,
	MEG2BPFILXNX,
	XE10G2BPIT,
	XE10G2BPICX4,
	XE10G2BPISR,
	XE10G2BPILR,
	PEG4BPIIO,
	XE10G2BPIXR,
	PE10GDBISR,
	PE10GDBILR,
	PEG2BISC6,
	PEG6BPIFC,
	PE10G2BPTCX4,
	PE10G2BPTSR,
	PE10G2BPTLR,
	PE10G2BPTT,
	PEG4BPI6,
	PEG4BPFI6,
	PEG4BPFI6LX,
	PEG4BPFI6ZX,
	PEG2BPI6,
	PEG2BPFI6,
	PEG2BPFI6LX,
	PEG2BPFI6ZX,
	PEG2BPFI6FLXM,
	PEG4BPI6FC,
	PEG4BPFI6FC,
	PEG4BPFI6FCLX,
	PEG4BPFI6FCZX,
	PEG6BPI6,
	PEG2BPI6SC6,
	MEG2BPI6,
	XEG2BPI6,
	MEG4BPI6,
	PEG2BPFI5,
	PEG2BPFI5LX,
	PXEG4BPFI,
	M1EG2BPI6,
	M1EG2BPFI6,
	M1EG2BPFI6LX,
	M1EG2BPFI6ZX,
	M1EG4BPI6,
	M1EG4BPFI6,
	M1EG4BPFI6LX,
	M1EG4BPFI6ZX,
	M1EG6BPI6,
	M1E2G4BPi80,
	M1E2G4BPFi80,
	M1E2G4BPFi80LX,
	M1E2G4BPFi80ZX,
	PE210G2SPI9,
	M1E10G2BPI9CX4,
	M1E10G2BPI9SR,
	M1E10G2BPI9LR,
	M1E10G2BPI9T,
	PE210G2BPI9CX4,
	PE210G2BPI9SR,
	PE210G2BPI9LR,
	PE210G2BPI9T,
	M2EG2BPFI6,
	M2EG2BPFI6LX,
	M2EG2BPFI6ZX,
	M2EG4BPI6,
	M2EG4BPFI6,
	M2EG4BPFI6LX,
	M2EG4BPFI6ZX,
	M2EG6BPI6,
	PEG2DBI6,
	PEG2DBFI6,
	PEG2DBFI6LX,
	PEG2DBFI6ZX,
	PE2G4BPi80,
	PE2G4BPFi80,
	PE2G4BPFi80LX,
	PE2G4BPFi80ZX,
	PE2G4BPi80L,
	M6E2G8BPi80A,

	PE2G2BPi35,
	PAC1200BPi35,
	PE2G2BPFi35,
	PE2G2BPFi35LX,
	PE2G2BPFi35ZX,
	PE2G4BPi35,
	PE2G4BPi35L,
	PE2G4BPFi35,
	PE2G4BPFi35LX,
	PE2G4BPFi35ZX,

	PE2G6BPi35,
	PE2G6BPi35CX,

	PE2G2BPi80,
	PE2G2BPFi80,
	PE2G2BPFi80LX,
	PE2G2BPFi80ZX,
	M2E10G2BPI9CX4,
	M2E10G2BPI9SR,
	M2E10G2BPI9LR,
	M2E10G2BPI9T,
	M6E2G8BPi80,
	PE210G2DBi9SR,
	PE210G2DBi9SRRB,
	PE210G2DBi9LR,
	PE210G2DBi9LRRB,
	PE310G4DBi940SR,
	PE310G4BPi9T,
	PE310G4BPi9SR,
	PE310G4BPi9LR,
	PE210G2BPi40,
} board_t;

typedef struct _bpmod_info_t {
	unsigned int vendor;
	unsigned int device;
	unsigned int subvendor;
	unsigned int subdevice;
	unsigned int index;
	char *bp_name;

} bpmod_info_t;

typedef struct _dev_desc {
	char *name;
} dev_desc_t;

dev_desc_t dev_desc[] = {
	{"Silicom Bypass PXG2BPFI-SD series adapter"},
	{"Silicom Bypass PXG2BPFIL-SD series adapter"},
	{"Silicom Bypass PXG2BPFILX-SD series adapter"},
	{"Silicom Bypass PXG2BPFILLX-SD series adapter"},
	{"Silicom Bypass PXG2BPI-SD series adapter"},
	{"Silicom Bypass PXG2BPIG-SD series adapter"},
	{"Silicom Bypass PXG2TBFI-SD series adapter"},
	{"Silicom Bypass PXG4BPI-SD series adapter"},
	{"Silicom Bypass PXG4BPFI-SD series adapter"},
	{"Silicom Bypass PEG4BPI-SD series adapter"},
	{"Silicom Bypass PEG2BPI-SD series adapter"},
	{"Silicom Bypass PEG4BPIN-SD series adapter"},
	{"Silicom Bypass PEG2BPFI-SD series adapter"},
	{"Silicom Bypass PEG2BPFI-LX-SD series adapter"},
	{"Silicom Bypass PMCX2BPFI-SD series adapter"},
	{"Silicom Bypass PMCX2BPFI-N series adapter"},
	{"Intel Bypass PEG2BPII series adapter"},
	{"Intel Bypass PEG2BPFII series adapter"},
	{"Silicom Bypass PXG4BPFILX-SD series adapter"},
	{"Silicom Bypass PMCX2BPI-N series adapter"},
	{"Silicom Bypass PMCX4BPI-N series adapter"},
	{"Silicom Bypass PXG2BISC1-SD series adapter"},
	{"Silicom Bypass PEG2TBFI-SD series adapter"},
	{"Silicom Bypass PXG2TBI-SD series adapter"},
	{"Silicom Bypass PXG4BPFID-SD series adapter"},
	{"Silicom Bypass PEG4BPFI-SD series adapter"},
	{"Silicom Bypass PEG4BPIPT-SD series adapter"},
	{"Silicom Bypass PXG6BPI-SD series adapter"},
	{"Silicom Bypass PEG4BPIL-SD series adapter"},
	{"Silicom Bypass PMCX2BPI-N2 series adapter"},
	{"Silicom Bypass PMCX4BPI-N2 series adapter"},
	{"Silicom Bypass PMCX2BPI-SD series adapter"},
	{"Silicom Bypass PEG2BPFID-SD series adapter"},
	{"Silicom Bypass PEG2BPFIDLX-SD series adapter"},
	{"Silicom Bypass PMCX4BPI-SD series adapter"},
	{"Silicom Bypass MEG2BPFILN-SD series adapter"},
	{"Silicom Bypass MEG2BPFINX-SD series adapter"},
	{"Silicom Bypass PEG4BPFILX-SD series adapter"},
	{"Silicom Bypass PE10G2BPISR-SD series adapter"},
	{"Silicom Bypass PE10G2BPILR-SD series adapter"},
	{"Silicom Bypass MHIO8AD-SD series adapter"},
	{"Silicom Bypass PE10G2BPICX4-SD series adapter"},
	{"Silicom Bypass PEG2BPI5-SD series adapter"},
	{"Silicom Bypass PEG6BPI5-SD series adapter"},
	{"Silicom Bypass PEG4BPFI5-SD series adapter"},
	{"Silicom Bypass PEG4BPFI5LX-SD series adapter"},
	{"Silicom Bypass MEG2BPFILXLN-SD series adapter"},
	{"Silicom Bypass PEG2BPIX1-SD series adapter"},
	{"Silicom Bypass MEG2BPFILXNX-SD series adapter"},
	{"Silicom Bypass XE10G2BPIT-SD series adapter"},
	{"Silicom Bypass XE10G2BPICX4-SD series adapter"},
	{"Silicom Bypass XE10G2BPISR-SD series adapter"},
	{"Silicom Bypass XE10G2BPILR-SD series adapter"},
	{"Intel Bypass PEG2BPFII0 series adapter"},
	{"Silicom Bypass XE10G2BPIXR series adapter"},
	{"Silicom Bypass PE10G2DBISR series adapter"},
	{"Silicom Bypass PEG2BI5SC6 series adapter"},
	{"Silicom Bypass PEG6BPI5FC series adapter"},

	{"Silicom Bypass PE10G2BPTCX4 series adapter"},
	{"Silicom Bypass PE10G2BPTSR series adapter"},
	{"Silicom Bypass PE10G2BPTLR series adapter"},
	{"Silicom Bypass PE10G2BPTT series adapter"},
	{"Silicom Bypass PEG4BPI6 series adapter"},
	{"Silicom Bypass PEG4BPFI6 series adapter"},
	{"Silicom Bypass PEG4BPFI6LX series adapter"},
	{"Silicom Bypass PEG4BPFI6ZX series adapter"},
	{"Silicom Bypass PEG2BPI6 series adapter"},
	{"Silicom Bypass PEG2BPFI6 series adapter"},
	{"Silicom Bypass PEG2BPFI6LX series adapter"},
	{"Silicom Bypass PEG2BPFI6ZX series adapter"},
	{"Silicom Bypass PEG2BPFI6FLXM series adapter"},
	{"Silicom Bypass PEG4BPI6FC series adapter"},
	{"Silicom Bypass PEG4BPFI6FC series adapter"},
	{"Silicom Bypass PEG4BPFI6FCLX series adapter"},
	{"Silicom Bypass PEG4BPFI6FCZX series adapter"},
	{"Silicom Bypass PEG6BPI6 series adapter"},
	{"Silicom Bypass PEG2BPI6SC6 series adapter"},
	{"Silicom Bypass MEG2BPI6 series adapter"},
	{"Silicom Bypass XEG2BPI6 series adapter"},
	{"Silicom Bypass MEG4BPI6 series adapter"},
	{"Silicom Bypass PEG2BPFI5-SD series adapter"},
	{"Silicom Bypass PEG2BPFI5LX-SD series adapter"},
	{"Silicom Bypass PXEG4BPFI-SD series adapter"},
	{"Silicom Bypass MxEG2BPI6 series adapter"},
	{"Silicom Bypass MxEG2BPFI6 series adapter"},
	{"Silicom Bypass MxEG2BPFI6LX series adapter"},
	{"Silicom Bypass MxEG2BPFI6ZX series adapter"},
	{"Silicom Bypass MxEG4BPI6 series adapter"},
	{"Silicom Bypass MxEG4BPFI6 series adapter"},
	{"Silicom Bypass MxEG4BPFI6LX series adapter"},
	{"Silicom Bypass MxEG4BPFI6ZX series adapter"},
	{"Silicom Bypass MxEG6BPI6 series adapter"},
	{"Silicom Bypass MxE2G4BPi80 series adapter"},
	{"Silicom Bypass MxE2G4BPFi80 series adapter"},
	{"Silicom Bypass MxE2G4BPFi80LX series adapter"},
	{"Silicom Bypass MxE2G4BPFi80ZX series adapter"},

	{"Silicom Bypass PE210G2SPI9 series adapter"},

	{"Silicom Bypass MxE210G2BPI9CX4 series adapter"},
	{"Silicom Bypass MxE210G2BPI9SR series adapter"},
	{"Silicom Bypass MxE210G2BPI9LR series adapter"},
	{"Silicom Bypass MxE210G2BPI9T series adapter"},

	{"Silicom Bypass PE210G2BPI9CX4 series adapter"},
	{"Silicom Bypass PE210G2BPI9SR series adapter"},
	{"Silicom Bypass PE210G2BPI9LR series adapter"},
	{"Silicom Bypass PE210G2BPI9T series adapter"},

	{"Silicom Bypass M2EG2BPFI6 series adapter"},
	{"Silicom Bypass M2EG2BPFI6LX series adapter"},
	{"Silicom Bypass M2EG2BPFI6ZX series adapter"},
	{"Silicom Bypass M2EG4BPI6 series adapter"},
	{"Silicom Bypass M2EG4BPFI6 series adapter"},
	{"Silicom Bypass M2EG4BPFI6LX series adapter"},
	{"Silicom Bypass M2EG4BPFI6ZX series adapter"},
	{"Silicom Bypass M2EG6BPI6 series adapter"},

	{"Silicom Bypass PEG2DBI6    series adapter"},
	{"Silicom Bypass PEG2DBFI6   series adapter"},
	{"Silicom Bypass PEG2DBFI6LX series adapter"},
	{"Silicom Bypass PEG2DBFI6ZX series adapter"},

	{"Silicom Bypass PE2G4BPi80 series adapter"},
	{"Silicom Bypass PE2G4BPFi80 series adapter"},
	{"Silicom Bypass PE2G4BPFi80LX series adapter"},
	{"Silicom Bypass PE2G4BPFi80ZX series adapter"},

	{"Silicom Bypass PE2G4BPi80L series adapter"},
	{"Silicom Bypass MxE2G8BPi80A series adapter"},

	{"Silicom Bypass PE2G2BPi35 series adapter"},
	{"Silicom Bypass PAC1200BPi35 series adapter"},
	{"Silicom Bypass PE2G2BPFi35 series adapter"},
	{"Silicom Bypass PE2G2BPFi35LX series adapter"},
	{"Silicom Bypass PE2G2BPFi35ZX series adapter"},

	{"Silicom Bypass PE2G4BPi35 series adapter"},
	{"Silicom Bypass PE2G4BPi35L series adapter"},
	{"Silicom Bypass PE2G4BPFi35 series adapter"},
	{"Silicom Bypass PE2G4BPFi35LX series adapter"},
	{"Silicom Bypass PE2G4BPFi35ZX series adapter"},

	{"Silicom Bypass PE2G6BPi35 series adapter"},
	{"Silicom Bypass PE2G6BPi35CX series adapter"},

	{"Silicom Bypass PE2G2BPi80 series adapter"},
	{"Silicom Bypass PE2G2BPFi80 series adapter"},
	{"Silicom Bypass PE2G2BPFi80LX series adapter"},
	{"Silicom Bypass PE2G2BPFi80ZX series adapter"},

	{"Silicom Bypass M2E10G2BPI9CX4 series adapter"},
	{"Silicom Bypass M2E10G2BPI9SR series adapter"},
	{"Silicom Bypass M2E10G2BPI9LR series adapter"},
	{"Silicom Bypass M2E10G2BPI9T series adapter"},
	{"Silicom Bypass MxE2G8BPi80 series adapter"},
	{"Silicom Bypass PE210G2DBi9SR series adapter"},
	{"Silicom Bypass PE210G2DBi9SRRB series adapter"},
	{"Silicom Bypass PE210G2DBi9LR series adapter"},
	{"Silicom Bypass PE210G2DBi9LRRB series adapter"},
	{"Silicom Bypass PE310G4DBi9-SR series adapter"},
	{"Silicom Bypass PE310G4BPi9T series adapter"},
	{"Silicom Bypass PE310G4BPi9SR series adapter"},
	{"Silicom Bypass PE310G4BPi9LR series adapter"},
	{"Silicom Bypass PE210G2BPi40T series adapter"},
	{0},
};

static bpmod_info_t tx_ctl_pci_tbl[] = {
	{0x8086, 0x107a, SILICOM_SVID, SILICOM_PXG2BPFI_SSID, PXG2BPFI,
	 "PXG2BPFI-SD"},
	{0x8086, 0x107a, SILICOM_SVID, SILICOM_PXG2BPFIL_SSID, PXG2BPFIL,
	 "PXG2BPFIL-SD"},
	{0x8086, 0x107a, SILICOM_SVID, SILICOM_PXG2BPFILX_SSID, PXG2BPFILX,
	 "PXG2BPFILX-SD"},
	{0x8086, 0x107a, SILICOM_SVID, SILICOM_PXG2BPFILLX_SSID, PXG2BPFILLX,
	 "PXG2BPFILLXSD"},
	{0x8086, 0x1010, SILICOM_SVID, SILICOM_PXGBPI_SSID, PXGBPI,
	 "PXG2BPI-SD"},
	{0x8086, 0x1079, SILICOM_SVID, SILICOM_PXGBPIG_SSID, PXGBPIG,
	 "PXG2BPIG-SD"},
	{0x8086, 0x107a, SILICOM_SVID, SILICOM_PXG2TBFI_SSID, PXG2TBFI,
	 "PXG2TBFI-SD"},
	{0x8086, 0x1079, SILICOM_SVID, SILICOM_PXG4BPI_SSID, PXG4BPI,
	 "PXG4BPI-SD"},
	{0x8086, 0x107a, SILICOM_SVID, SILICOM_PXG4BPFI_SSID, PXG4BPFI,
	 "PXG4BPFI-SD"},
	{0x8086, 0x107a, SILICOM_SVID, SILICOM_PXG4BPFILX_SSID, PXG4BPFILX,
	 "PXG4BPFILX-SD"},
	{0x8086, 0x1079, SILICOM_SVID, SILICOM_PEG4BPI_SSID, PEG4BPI,
	 "PEXG4BPI-SD"},
	{0x8086, 0x105e, SILICOM_SVID, SILICOM_PEG2BPI_SSID, PEG2BPI,
	 "PEG2BPI-SD"},
	{0x8086, 0x105e, SILICOM_SVID, SILICOM_PEG4BPIN_SSID, PEG4BPIN,
	 "PEG4BPI-SD"},
	{0x8086, 0x105f, SILICOM_SVID, SILICOM_PEG2BPFI_SSID, PEG2BPFI,
	 "PEG2BPFI-SD"},
	{0x8086, 0x105f, SILICOM_SVID, SILICOM_PEG2BPFILX_SSID, PEG2BPFILX,
	 "PEG2BPFILX-SD"},
	{0x8086, 0x107a, SILICOM_SVID, SILICOM_PMCXG2BPFI_SSID, PMCXG2BPFI,
	 "PMCX2BPFI-SD"},
	{0x8086, 0x107a, NOKIA_PMCXG2BPFIN_SVID, NOKIA_PMCXG2BPFIN_SSID,
	 PMCXG2BPFIN, "PMCX2BPFI-N"},
	{0x8086, INTEL_PEG4BPII_PID, 0x8086, INTEL_PEG4BPII_SSID, PEG4BPII,
	 "PEG4BPII"},
	{0x8086, INTEL_PEG4BPIIO_PID, 0x8086, INTEL_PEG4BPIIO_SSID, PEG4BPIIO,
	 "PEG4BPII0"},
	{0x8086, INTEL_PEG4BPFII_PID, 0x8086, INTEL_PEG4BPFII_SSID, PEG4BPFII,
	 "PEG4BPFII"},
	{0x8086, 0x1079, NOKIA_PMCXG2BPFIN_SVID, NOKIA_PMCXG2BPIN_SSID,
	 PMCXG2BPIN, "PMCX2BPI-N"},
	{0x8086, 0x1079, NOKIA_PMCXG2BPFIN_SVID, NOKIA_PMCXG4BPIN_SSID,
	 PMCXG4BPIN, "PMCX4BPI-N"},
	{0x8086, 0x1079, SILICOM_SVID, SILICOM_PXG2BISC1_SSID, PXG2BISC1,
	 "PXG2BISC1-SD"},
	{0x8086, 0x105f, SILICOM_SVID, SILICOM_PEG2TBFI_SSID, PEG2TBFI,
	 "PEG2TBFI-SD"},
	{0x8086, 0x1079, SILICOM_SVID, SILICOM_PXG2TBI_SSID, PXG2TBI,
	 "PXG2TBI-SD"},
	{0x8086, 0x107a, SILICOM_SVID, SILICOM_PXG4BPFID_SSID, PXG4BPFID,
	 "PXG4BPFID-SD"},
	{0x8086, 0x105f, SILICOM_SVID, SILICOM_PEG4BPFI_SSID, PEG4BPFI,
	 "PEG4BPFI-SD"},
	{0x8086, 0x105e, SILICOM_SVID, SILICOM_PEG4BPIPT_SSID, PEG4BPIPT,
	 "PEG4BPIPT-SD"},
	{0x8086, 0x1079, SILICOM_SVID, SILICOM_PXG6BPI_SSID, PXG6BPI,
	 "PXG6BPI-SD"},
	{0x8086, 0x10a7, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG4BPIL_SSID /*PCI_ANY_ID */ , PEG4BPIL, "PEG4BPIL-SD"},
	{0x8086, 0x1079, NOKIA_PMCXG2BPFIN_SVID, NOKIA_PMCXG2BPIN2_SSID,
	 PMCXG2BPIN2, "PMCX2BPI-N2"},
	{0x8086, 0x1079, NOKIA_PMCXG2BPFIN_SVID, NOKIA_PMCXG4BPIN2_SSID,
	 PMCXG4BPIN2, "PMCX4BPI-N2"},
	{0x8086, 0x1079, SILICOM_SVID, SILICOM_PMCX2BPI_SSID, PMCX2BPI,
	 "PMCX2BPI-SD"},
	{0x8086, 0x1079, SILICOM_SVID, SILICOM_PMCX4BPI_SSID, PMCX4BPI,
	 "PMCX4BPI-SD"},
	{0x8086, 0x105f, SILICOM_SVID, SILICOM_PEG2BPFID_SSID, PEG2BPFID,
	 "PEG2BPFID-SD"},
	{0x8086, 0x105f, SILICOM_SVID, SILICOM_PEG2BPFIDLX_SSID, PEG2BPFIDLX,
	 "PEG2BPFIDLXSD"},
	{0x8086, 0x105f, SILICOM_SVID, SILICOM_MEG2BPFILN_SSID, MEG2BPFILN,
	 "MEG2BPFILN-SD"},
	{0x8086, 0x105f, SILICOM_SVID, SILICOM_MEG2BPFINX_SSID, MEG2BPFINX,
	 "MEG2BPFINX-SD"},
	{0x8086, 0x105f, SILICOM_SVID, SILICOM_PEG4BPFILX_SSID, PEG4BPFILX,
	 "PEG4BPFILX-SD"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID, SILICOM_PE10G2BPISR_SSID,
	 PE10G2BPISR, "PE10G2BPISR"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID, SILICOM_PE10G2BPILR_SSID,
	 PE10G2BPILR, "PE10G2BPILR"},
	{0x8086, 0x10a9, SILICOM_SVID, SILICOM_MHIO8AD_SSID, MHIO8AD,
	 "MHIO8AD-SD"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID, SILICOM_PE10G2BPICX4_SSID,
	 PE10G2BPISR, "PE10G2BPICX4"},
	{0x8086, 0x10a7, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG2BPI5_SSID /*PCI_ANY_ID */ , PEG2BPI5, "PEG2BPI5-SD"},
	{0x8086, 0x10a7, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG6BPI_SSID /*PCI_ANY_ID */ , PEG6BPI, "PEG6BPI5"},
	{0x8086, 0x10a9, SILICOM_SVID /*PCI_ANY_ID */ , SILICOM_PEG4BPFI5_SSID,
	 PEG4BPFI5, "PEG4BPFI5"},
	{0x8086, 0x10a9, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG4BPFI5LX_SSID, PEG4BPFI5LX, "PEG4BPFI5LX"},
	{0x8086, 0x105f, SILICOM_SVID, SILICOM_MEG2BPFILXLN_SSID, MEG2BPFILXLN,
	 "MEG2BPFILXLN"},
	{0x8086, 0x105e, SILICOM_SVID, SILICOM_PEG2BPIX1_SSID, PEG2BPIX1,
	 "PEG2BPIX1-SD"},
	{0x8086, 0x105f, SILICOM_SVID, SILICOM_MEG2BPFILXNX_SSID, MEG2BPFILXNX,
	 "MEG2BPFILXNX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID, SILICOM_XE10G2BPIT_SSID, XE10G2BPIT,
	 "XE10G2BPIT"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID, SILICOM_XE10G2BPICX4_SSID,
	 XE10G2BPICX4, "XE10G2BPICX4"},
	{0x8086, 0x10C6, SILICOM_SVID, SILICOM_XE10G2BPISR_SSID, XE10G2BPISR,
	 "XE10G2BPISR"},
	{0x8086, 0x10C6, SILICOM_SVID, SILICOM_XE10G2BPILR_SSID, XE10G2BPILR,
	 "XE10G2BPILR"},
	{0x8086, 0x10C6, NOKIA_XE10G2BPIXR_SVID, NOKIA_XE10G2BPIXR_SSID,
	 XE10G2BPIXR, "XE10G2BPIXR"},
	{0x8086, 0x10C6, SILICOM_SVID, SILICOM_PE10GDBISR_SSID, PE10GDBISR,
	 "PE10G2DBISR"},
	{0x8086, 0x10C6, SILICOM_SVID, SILICOM_PE10GDBILR_SSID, PE10GDBILR,
	 "PE10G2DBILR"},
	{0x8086, 0x10a7, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG2BISC6_SSID /*PCI_ANY_ID */ , PEG2BISC6, "PEG2BI5SC6"},
	{0x8086, 0x10a7, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG6BPIFC_SSID /*PCI_ANY_ID */ , PEG6BPIFC, "PEG6BPI5FC"},

	{BROADCOM_VID, BROADCOM_PE10G2_PID, SILICOM_SVID,
	 SILICOM_PE10G2BPTCX4_SSID, PE10G2BPTCX4, "PE10G2BPTCX4"},
	{BROADCOM_VID, BROADCOM_PE10G2_PID, SILICOM_SVID,
	 SILICOM_PE10G2BPTSR_SSID, PE10G2BPTSR, "PE10G2BPTSR"},
	{BROADCOM_VID, BROADCOM_PE10G2_PID, SILICOM_SVID,
	 SILICOM_PE10G2BPTLR_SSID, PE10G2BPTLR, "PE10G2BPTLR"},
	{BROADCOM_VID, BROADCOM_PE10G2_PID, SILICOM_SVID,
	 SILICOM_PE10G2BPTT_SSID, PE10G2BPTT, "PE10G2BPTT"},

	/* {BROADCOM_VID, BROADCOM_PE10G2_PID, PCI_ANY_ID, PCI_ANY_ID, PE10G2BPTCX4, "PE10G2BPTCX4"}, */

	{0x8086, 0x10c9, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG4BPI6_SSID /*PCI_ANY_ID */ , PEG4BPI6, "PEG4BPI6"},
	{0x8086, 0x10e6, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG4BPFI6_SSID /*PCI_ANY_ID */ , PEG4BPFI6, "PEG4BPFI6"},
	{0x8086, 0x10e6, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG4BPFI6LX_SSID /*PCI_ANY_ID */ , PEG4BPFI6LX, "PEG4BPFI6LX"},
	{0x8086, 0x10e6, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG4BPFI6ZX_SSID /*PCI_ANY_ID */ , PEG4BPFI6ZX, "PEG4BPFI6ZX"},
	{0x8086, 0x10c9, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG2BPI6_SSID /*PCI_ANY_ID */ , PEG2BPI6, "PEG2BPI6"},
	{0x8086, 0x10e6, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG2BPFI6_SSID /*PCI_ANY_ID */ , PEG2BPFI6, "PEG2BPFI6"},
	{0x8086, 0x10e6, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG2BPFI6LX_SSID /*PCI_ANY_ID */ , PEG2BPFI6LX, "PEG2BPFI6LX"},
	{0x8086, 0x10e6, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG2BPFI6ZX_SSID /*PCI_ANY_ID */ , PEG2BPFI6ZX, "PEG2BPFI6ZX"},
	{0x8086, 0x10e7, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG2BPFI6FLXM_SSID /*PCI_ANY_ID */ , PEG2BPFI6FLXM,
	 "PEG2BPFI6FLXM"},
	{0x8086, 0x10c9, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG4BPI6FC_SSID /*PCI_ANY_ID */ , PEG4BPI6FC, "PEG4BPI6FC"},
	{0x8086, 0x10e6, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG4BPFI6FC_SSID /*PCI_ANY_ID */ , PEG4BPFI6FC, "PEG4BPFI6FC"},
	{0x8086, 0x10e6, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG4BPFI6FCLX_SSID /*PCI_ANY_ID */ , PEG4BPFI6FCLX,
	 "PEG4BPFI6FCLX"},
	{0x8086, 0x10e6, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG4BPFI6FCZX_SSID /*PCI_ANY_ID */ , PEG4BPFI6FCZX,
	 "PEG4BPFI6FCZX"},
	{0x8086, 0x10c9, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG6BPI6_SSID /*PCI_ANY_ID */ , PEG6BPI6, "PEG6BPI6"},
	{0x8086, 0x10c9, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG2BPI6SC6_SSID /*PCI_ANY_ID */ , PEG2BPI6SC6,
	 "PEG6BPI62SC6"},
	{0x8086, 0x10c9, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_MEG2BPI6_SSID /*PCI_ANY_ID */ , MEG2BPI6, "MEG2BPI6"},
	{0x8086, 0x10c9, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_XEG2BPI6_SSID /*PCI_ANY_ID */ , XEG2BPI6, "XEG2BPI6"},
	{0x8086, 0x10c9, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_MEG4BPI6_SSID /*PCI_ANY_ID */ , MEG4BPI6, "MEG4BPI6"},

	{0x8086, 0x10a9, SILICOM_SVID /*PCI_ANY_ID */ , SILICOM_PEG2BPFI5_SSID,
	 PEG2BPFI5, "PEG2BPFI5"},
	{0x8086, 0x10a9, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG2BPFI5LX_SSID, PEG2BPFI5LX, "PEG2BPFI5LX"},

	{0x8086, 0x105f, SILICOM_SVID, SILICOM_PXEG4BPFI_SSID, PXEG4BPFI,
	 "PXEG4BPFI-SD"},

	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M1EG2BPI6_SSID /*PCI_ANY_ID */ , M1EG2BPI6, "MxEG2BPI6"},

	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M1EG2BPFI6_SSID /*PCI_ANY_ID */ , M1EG2BPFI6, "MxEG2BPFI6"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M1EG2BPFI6LX_SSID /*PCI_ANY_ID */ , M1EG2BPFI6LX,
	 "MxEG2BPFI6LX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M1EG2BPFI6ZX_SSID /*PCI_ANY_ID */ , M1EG2BPFI6ZX,
	 "MxEG2BPFI6ZX"},

	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M1EG4BPI6_SSID /*PCI_ANY_ID */ , M1EG4BPI6, "MxEG4BPI6"},

	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M1EG4BPFI6_SSID /*PCI_ANY_ID */ , M1EG4BPFI6, "MxEG4BPFI6"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M1EG4BPFI6LX_SSID /*PCI_ANY_ID */ , M1EG4BPFI6LX,
	 "MxEG4BPFI6LX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M1EG4BPFI6ZX_SSID /*PCI_ANY_ID */ , M1EG4BPFI6ZX,
	 "MxEG4BPFI6ZX"},

	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M1EG6BPI6_SSID /*PCI_ANY_ID */ , M1EG6BPI6, "MxEG6BPI6"},

	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M1E2G4BPi80_SSID /*PCI_ANY_ID */ , M1E2G4BPi80, "MxE2G4BPi80"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M1E2G4BPFi80_SSID /*PCI_ANY_ID */ , M1E2G4BPFi80,
	 "MxE2G4BPFi80"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M1E2G4BPFi80LX_SSID /*PCI_ANY_ID */ , M1E2G4BPFi80LX,
	 "MxE2G4BPFi80LX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M1E2G4BPFi80ZX_SSID /*PCI_ANY_ID */ , M1E2G4BPFi80ZX,
	 "MxE2G4BPFi80ZX"},

	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M2EG2BPFI6_SSID /*PCI_ANY_ID */ , M2EG2BPFI6, "M2EG2BPFI6"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M2EG2BPFI6LX_SSID /*PCI_ANY_ID */ , M2EG2BPFI6LX,
	 "M2EG2BPFI6LX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M2EG2BPFI6ZX_SSID /*PCI_ANY_ID */ , M2EG2BPFI6ZX,
	 "M2EG2BPFI6ZX"},

	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M2EG4BPI6_SSID /*PCI_ANY_ID */ , M2EG4BPI6, "M2EG4BPI6"},

	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M2EG4BPFI6_SSID /*PCI_ANY_ID */ , M2EG4BPFI6, "M2EG4BPFI6"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M2EG4BPFI6LX_SSID /*PCI_ANY_ID */ , M2EG4BPFI6LX,
	 "M2EG4BPFI6LX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M2EG4BPFI6ZX_SSID /*PCI_ANY_ID */ , M2EG4BPFI6ZX,
	 "M2EG4BPFI6ZX"},

	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M2EG6BPI6_SSID /*PCI_ANY_ID */ , M2EG6BPI6, "M2EG6BPI6"},

	{0x8086, 0x10c9, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG2DBI6_SSID /*PCI_ANY_ID */ , PEG2DBI6, "PEG2DBI6"},
	{0x8086, 0x10e6, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG2DBFI6_SSID /*PCI_ANY_ID */ , PEG2DBFI6, "PEG2DBFI6"},
	{0x8086, 0x10e6, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG2DBFI6LX_SSID /*PCI_ANY_ID */ , PEG2DBFI6LX, "PEG2DBFI6LX"},
	{0x8086, 0x10e6, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PEG2DBFI6ZX_SSID /*PCI_ANY_ID */ , PEG2DBFI6ZX, "PEG2DBFI6ZX"},

	{0x8086, 0x10F9, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE210G2DBi9SR_SSID, PE210G2DBi9SR, "PE210G2DBi9SR"},
	{0x8086, 0x10F9, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE210G2DBi9LR_SSID, PE210G2DBi9LR, "PE210G2DBi9LR"},
	{0x8086, 0x10F9, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE310G4DBi940SR_SSID, PE310G4DBi940SR, "PE310G4DBi9SR"},

	{0x8086, 0x10Fb, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE310G4BPi9T_SSID, PE310G4BPi9T, "PE310G4BPi9T"},
	{0x8086, 0x10Fb, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE310G4BPi9SR_SSID, PE310G4BPi9SR, "PE310G4BPi9SR"},
	{0x8086, 0x10Fb, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE310G4BPi9LR_SSID, PE310G4BPi9LR, "PE310G4BPi9LR"},

	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE2G4BPi80_SSID /*PCI_ANY_ID */ , PE2G4BPi80, "PE2G4BPi80"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE2G4BPFi80_SSID /*PCI_ANY_ID */ , PE2G4BPFi80, "PE2G4BPFi80"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE2G4BPFi80LX_SSID /*PCI_ANY_ID */ , PE2G4BPFi80LX,
	 "PE2G4BPFi80LX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE2G4BPFi80ZX_SSID /*PCI_ANY_ID */ , PE2G4BPFi80ZX,
	 "PE2G4BPFi80ZX"},

	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE2G4BPi80L_SSID /*PCI_ANY_ID */ , PE2G4BPi80L, "PE2G4BPi80L"},

	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M6E2G8BPi80A_SSID /*PCI_ANY_ID */ , M6E2G8BPi80A,
	 "MxE2G8BPi80A"},

	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE2G2BPi35_SSID /*PCI_ANY_ID */ , PE2G2BPi35, "PE2G2BPi35"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PAC1200BPi35_SSID /*PCI_ANY_ID */ , PAC1200BPi35,
	 "PAC1200BPi35"},

	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE2G2BPFi35_SSID /*PCI_ANY_ID */ , PE2G2BPFi35, "PE2G2BPFi35"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE2G2BPFi35LX_SSID /*PCI_ANY_ID */ , PE2G2BPFi35LX,
	 "PE2G2BPFi35LX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE2G2BPFi35ZX_SSID /*PCI_ANY_ID */ , PE2G2BPFi35ZX,
	 "PE2G2BPFi35ZX"},

	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE2G4BPi35_SSID /*PCI_ANY_ID */ , PE2G4BPi35, "PE2G4BPi35"},

	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE2G4BPi35L_SSID /*PCI_ANY_ID */ , PE2G4BPi35L, "PE2G4BPi35L"},

	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE2G4BPFi35_SSID /*PCI_ANY_ID */ , PE2G4BPFi35, "PE2G4BPFi35"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE2G4BPFi35LX_SSID /*PCI_ANY_ID */ , PE2G4BPFi35LX,
	 "PE2G4BPFi35LX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE2G4BPFi35ZX_SSID /*PCI_ANY_ID */ , PE2G4BPFi35ZX,
	 "PE2G4BPFi35ZX"},

	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE2G6BPi35_SSID /*PCI_ANY_ID */ , PE2G6BPi35, "PE2G6BPi35"},


	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xaa0, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xaa1, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xaa2, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xaa3, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xaa4, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xaa5, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xaa6, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xaa7, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xaa8, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xaa9, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xaaa, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xaab, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xaac, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xaad, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xaae, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xaaf, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xab0, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xab1, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xab2, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xab3, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xab4, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xab5, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xab6, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xab7, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xab8, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xab9, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xaba, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xabb, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xabc, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xabd, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xabe, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ , 0xabf, PE2G6BPi35CX,
	 "PE2G6BPi35CX"},

	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE2G2BPi80_SSID /*PCI_ANY_ID */ , PE2G2BPi80, "PE2G2BPi80"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE2G2BPFi80_SSID /*PCI_ANY_ID */ , PE2G2BPFi80, "PE2G2BPFi80"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE2G2BPFi80LX_SSID /*PCI_ANY_ID */ , PE2G2BPFi80LX,
	 "PE2G2BPFi80LX"},
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE2G2BPFi80ZX_SSID /*PCI_ANY_ID */ , PE2G2BPFi80ZX,
	 "PE2G2BPFi80ZX"},

	{0x8086, 0x10c9, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_MEG2BPI6_SSID /*PCI_ANY_ID */ , MEG2BPI6, "MEG2BPI6"},
	{0x8086, 0x10c9, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_XEG2BPI6_SSID /*PCI_ANY_ID */ , XEG2BPI6, "XEG2BPI6"},

#if 0
	{0x8086, 0x10fb, 0x8086, INTEL_PE210G2SPI9_SSID, PE210G2SPI9,
	 "PE210G2SPI9"},
#endif
	{0x8086, 0x10fb, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M1E10G2BPI9CX4_SSID /*PCI_ANY_ID */ , M1E10G2BPI9CX4,
	 "MxE210G2BPI9CX4"},
	{0x8086, 0x10fb, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M1E10G2BPI9SR_SSID /*PCI_ANY_ID */ , M1E10G2BPI9SR,
	 "MxE210G2BPI9SR"},
	{0x8086, 0x10fb, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M1E10G2BPI9LR_SSID /*PCI_ANY_ID */ , M1E10G2BPI9LR,
	 "MxE210G2BPI9LR"},
	{0x8086, 0x10fb, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M1E10G2BPI9T_SSID /*PCI_ANY_ID */ , M1E10G2BPI9T,
	 "MxE210G2BPI9T"},

	{0x8086, 0x10fb, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M2E10G2BPI9CX4_SSID /*PCI_ANY_ID */ , M2E10G2BPI9CX4,
	 "M2E10G2BPI9CX4"},
	{0x8086, 0x10fb, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M2E10G2BPI9SR_SSID /*PCI_ANY_ID */ , M2E10G2BPI9SR,
	 "M2E10G2BPI9SR"},
	{0x8086, 0x10fb, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M2E10G2BPI9LR_SSID /*PCI_ANY_ID */ , M2E10G2BPI9LR,
	 "M2E10G2BPI9LR"},
	{0x8086, 0x10fb, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M2E10G2BPI9T_SSID /*PCI_ANY_ID */ , M2E10G2BPI9T,
	 "M2E10G2BPI9T"},

	{0x8086, 0x10fb, SILICOM_SVID, SILICOM_PE210G2BPI9CX4_SSID,
	 PE210G2BPI9CX4, "PE210G2BPI9CX4"},
	{0x8086, 0x10fb, SILICOM_SVID, SILICOM_PE210G2BPI9SR_SSID,
	 PE210G2BPI9SR, "PE210G2BPI9SR"},
	{0x8086, 0x10fb, SILICOM_SVID, SILICOM_PE210G2BPI9LR_SSID,
	 PE210G2BPI9LR, "PE210G2BPI9LR"},
	{0x8086, 0x10fb, SILICOM_SVID, SILICOM_PE210G2BPI9T_SSID, PE210G2BPI9T,
	 "PE210G2BPI9T"},

#if 0
	{0x1374, 0x2c, SILICOM_SVID, SILICOM_PXG4BPI_SSID, PXG4BPI,
	 "PXG4BPI-SD"},

	{0x1374, 0x2d, SILICOM_SVID, SILICOM_PXG4BPFI_SSID, PXG4BPFI,
	 "PXG4BPFI-SD"},

	{0x1374, 0x3f, SILICOM_SVID, SILICOM_PXG2TBI_SSID, PXG2TBI,
	 "PXG2TBI-SD"},

	{0x1374, 0x3d, SILICOM_SVID, SILICOM_PXG2BISC1_SSID, PXG2BISC1,
	 "PXG2BISC1-SD"},

	{0x1374, 0x40, SILICOM_SVID, SILICOM_PEG4BPFI_SSID, PEG4BPFI,
	 "PEG4BPFI-SD"},

#ifdef BP_SELF_TEST
	{0x1374, 0x28, SILICOM_SVID, 0x28, PXGBPI, "PXG2BPI-SD"},
#endif
#endif
	{0x8086, PCI_ANY_ID, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_M6E2G8BPi80_SSID /*PCI_ANY_ID */ , M6E2G8BPi80, "MxE2G8BPi80"},
	{0x8086, 0x1528, SILICOM_SVID /*PCI_ANY_ID */ ,
	 SILICOM_PE210G2BPi40_SSID /*PCI_ANY_ID */ , PE210G2BPi40,
	 "PE210G2BPi40T"},

	/* required last entry */
	{0,}
};

/*
* Initialize the module - Register the character device
*/

static int __init bypass_init_module(void)
{
	int ret_val, idx, idx_dev = 0;
	struct pci_dev *pdev1 = NULL;
	unsigned long mmio_start, mmio_len;

	printk(BP_MOD_DESCR " v" BP_MOD_VER "\n");
	ret_val = register_chrdev(major_num, DEVICE_NAME, &Fops);
	if (ret_val < 0) {
		printk("%s failed with %d\n", DEVICE_NAME, ret_val);
		return ret_val;
	}
	major_num = ret_val;	/* dynamic */
	for (idx = 0; tx_ctl_pci_tbl[idx].vendor; idx++) {
		while ((pdev1 = pci_get_subsys(tx_ctl_pci_tbl[idx].vendor,
					       tx_ctl_pci_tbl[idx].device,
					       tx_ctl_pci_tbl[idx].subvendor,
					       tx_ctl_pci_tbl[idx].subdevice,
					       pdev1))) {

			device_num++;
		}
	}
	if (!device_num) {
		printk("No such device\n");
		unregister_chrdev(major_num, DEVICE_NAME);
		return -1;
	}

	bpctl_dev_arr = kmalloc((device_num) * sizeof(bpctl_dev_t), GFP_KERNEL);

	if (!bpctl_dev_arr) {
		printk("Allocation error\n");
		unregister_chrdev(major_num, DEVICE_NAME);
		return -1;
	}
	memset(bpctl_dev_arr, 0, ((device_num) * sizeof(bpctl_dev_t)));

	pdev1 = NULL;
	for (idx = 0; tx_ctl_pci_tbl[idx].vendor; idx++) {
		while ((pdev1 = pci_get_subsys(tx_ctl_pci_tbl[idx].vendor,
					       tx_ctl_pci_tbl[idx].device,
					       tx_ctl_pci_tbl[idx].subvendor,
					       tx_ctl_pci_tbl[idx].subdevice,
					       pdev1))) {
			bpctl_dev_arr[idx_dev].pdev = pdev1;

			mmio_start = pci_resource_start(pdev1, 0);
			mmio_len = pci_resource_len(pdev1, 0);

			bpctl_dev_arr[idx_dev].desc =
			    dev_desc[tx_ctl_pci_tbl[idx].index].name;
			bpctl_dev_arr[idx_dev].name =
			    tx_ctl_pci_tbl[idx].bp_name;
			bpctl_dev_arr[idx_dev].device =
			    tx_ctl_pci_tbl[idx].device;
			bpctl_dev_arr[idx_dev].vendor =
			    tx_ctl_pci_tbl[idx].vendor;
			bpctl_dev_arr[idx_dev].subdevice =
			    tx_ctl_pci_tbl[idx].subdevice;
			bpctl_dev_arr[idx_dev].subvendor =
			    tx_ctl_pci_tbl[idx].subvendor;
			/* bpctl_dev_arr[idx_dev].pdev=pdev1; */
			bpctl_dev_arr[idx_dev].func = PCI_FUNC(pdev1->devfn);
			bpctl_dev_arr[idx_dev].slot = PCI_SLOT(pdev1->devfn);
			bpctl_dev_arr[idx_dev].bus = pdev1->bus->number;
			bpctl_dev_arr[idx_dev].mem_map =
			    (unsigned long)ioremap(mmio_start, mmio_len);
#ifdef BP_SYNC_FLAG
			spin_lock_init(&bpctl_dev_arr[idx_dev].bypass_wr_lock);
#endif
			if (BP10G9_IF_SERIES(bpctl_dev_arr[idx_dev].subdevice))
				bpctl_dev_arr[idx_dev].bp_10g9 = 1;
			if (BP10G_IF_SERIES(bpctl_dev_arr[idx_dev].subdevice))
				bpctl_dev_arr[idx_dev].bp_10g = 1;
			if (PEG540_IF_SERIES(bpctl_dev_arr[idx_dev].subdevice)) {

				bpctl_dev_arr[idx_dev].bp_540 = 1;
			}
			if (PEGF5_IF_SERIES(bpctl_dev_arr[idx_dev].subdevice))
				bpctl_dev_arr[idx_dev].bp_fiber5 = 1;
			if (PEG80_IF_SERIES(bpctl_dev_arr[idx_dev].subdevice))
				bpctl_dev_arr[idx_dev].bp_i80 = 1;
			if (PEGF80_IF_SERIES(bpctl_dev_arr[idx_dev].subdevice))
				bpctl_dev_arr[idx_dev].bp_i80 = 1;
			if ((bpctl_dev_arr[idx_dev].subdevice & 0xa00) == 0xa00)
				bpctl_dev_arr[idx_dev].bp_i80 = 1;
			if (BP10GB_IF_SERIES(bpctl_dev_arr[idx_dev].subdevice)) {
				if (bpctl_dev_arr[idx_dev].ifindex == 0) {
					unregister_chrdev(major_num,
							  DEVICE_NAME);
					printk
					    ("Please load network driver for %s adapter!\n",
					     bpctl_dev_arr[idx_dev].name);
					return -1;
				}

				if (bpctl_dev_arr[idx_dev].ndev) {
					if (!
					    (bpctl_dev_arr[idx_dev].ndev->
					     flags & IFF_UP)) {
						if (!
						    (bpctl_dev_arr[idx_dev].
						     ndev->flags & IFF_UP)) {
							unregister_chrdev
							    (major_num,
							     DEVICE_NAME);
							printk
							    ("Please bring up network interfaces for %s adapter!\n",
							     bpctl_dev_arr
							     [idx_dev].name);
							return -1;
						}

					}
				}
				bpctl_dev_arr[idx_dev].bp_10gb = 1;
			}

			if (!bpctl_dev_arr[idx_dev].bp_10g9) {

				if (is_bypass_fn(&bpctl_dev_arr[idx_dev])) {
					printk(KERN_INFO "%s found, ",
					       bpctl_dev_arr[idx_dev].name);
					if ((OLD_IF_SERIES
					     (bpctl_dev_arr[idx_dev].subdevice))
					    ||
					    (INTEL_IF_SERIES
					     (bpctl_dev_arr[idx_dev].
					      subdevice)))
						bpctl_dev_arr[idx_dev].
						    bp_fw_ver = 0xff;
					else
						bpctl_dev_arr[idx_dev].
						    bp_fw_ver =
						    bypass_fw_ver(&bpctl_dev_arr
								  [idx_dev]);
					if ((bpctl_dev_arr[idx_dev].bp_10gb ==
					     1)
					    && (bpctl_dev_arr[idx_dev].
						bp_fw_ver == 0xff)) {
						int cnt = 100;
						while (cnt--) {
							iounmap((void
								 *)
								(bpctl_dev_arr
								 [idx_dev].
								 mem_map));
							mmio_start =
							    pci_resource_start
							    (pdev1, 0);
							mmio_len =
							    pci_resource_len
							    (pdev1, 0);

							bpctl_dev_arr[idx_dev].
							    mem_map =
							    (unsigned long)
							    ioremap(mmio_start,
								    mmio_len);

							bpctl_dev_arr[idx_dev].
							    bp_fw_ver =
							    bypass_fw_ver
							    (&bpctl_dev_arr
							     [idx_dev]);
							if (bpctl_dev_arr
							    [idx_dev].
							    bp_fw_ver == 0xa8)
								break;

						}
					}
					/* bpctl_dev_arr[idx_dev].bp_fw_ver=0xa8; */
					printk("firmware version: 0x%x\n",
					       bpctl_dev_arr[idx_dev].
					       bp_fw_ver);
				}
				bpctl_dev_arr[idx_dev].wdt_status =
				    WDT_STATUS_UNKNOWN;
				bpctl_dev_arr[idx_dev].reset_time = 0;
				atomic_set(&bpctl_dev_arr[idx_dev].wdt_busy, 0);
				bpctl_dev_arr[idx_dev].bp_status_un = 1;

				bypass_caps_init(&bpctl_dev_arr[idx_dev]);

				init_bypass_wd_auto(&bpctl_dev_arr[idx_dev]);
				init_bypass_tpl_auto(&bpctl_dev_arr[idx_dev]);
				if (NOKIA_SERIES
				    (bpctl_dev_arr[idx_dev].subdevice))
					reset_cont(&bpctl_dev_arr[idx_dev]);
			}
#ifdef BP_SELF_TEST
			if ((bpctl_dev_arr[idx_dev].bp_tx_data =
			     kmalloc(BPTEST_DATA_LEN, GFP_KERNEL))) {

				memset(bpctl_dev_arr[idx_dev].bp_tx_data, 0x0,
				       BPTEST_DATA_LEN);

				memset(bpctl_dev_arr[idx_dev].bp_tx_data, 0xff,
				       6);
				memset(bpctl_dev_arr[idx_dev].bp_tx_data + 6,
				       0x0, 1);
				memset(bpctl_dev_arr[idx_dev].bp_tx_data + 7,
				       0xaa, 5);

				*(__be16 *) (bpctl_dev_arr[idx_dev].bp_tx_data +
					     12) = htons(ETH_P_BPTEST);

			} else
				printk("bp_ctl: Memory allocation error!\n");
#endif
			idx_dev++;

		}
	}
	if_scan_init();

	sema_init(&bpctl_sema, 1);
	spin_lock_init(&bpvm_lock);
	{

		bpctl_dev_t *pbpctl_dev_c = NULL;
		for (idx_dev = 0;
		     ((bpctl_dev_arr[idx_dev].pdev != NULL)
		      && (idx_dev < device_num)); idx_dev++) {
			if (bpctl_dev_arr[idx_dev].bp_10g9) {
				pbpctl_dev_c =
				    get_status_port_fn(&bpctl_dev_arr[idx_dev]);
				if (is_bypass_fn(&bpctl_dev_arr[idx_dev])) {
					printk(KERN_INFO "%s found, ",
					       bpctl_dev_arr[idx_dev].name);
					bpctl_dev_arr[idx_dev].bp_fw_ver =
					    bypass_fw_ver(&bpctl_dev_arr
							  [idx_dev]);
					printk("firmware version: 0x%x\n",
					       bpctl_dev_arr[idx_dev].
					       bp_fw_ver);

				}
				bpctl_dev_arr[idx_dev].wdt_status =
				    WDT_STATUS_UNKNOWN;
				bpctl_dev_arr[idx_dev].reset_time = 0;
				atomic_set(&bpctl_dev_arr[idx_dev].wdt_busy, 0);
				bpctl_dev_arr[idx_dev].bp_status_un = 1;

				bypass_caps_init(&bpctl_dev_arr[idx_dev]);

				init_bypass_wd_auto(&bpctl_dev_arr[idx_dev]);
				init_bypass_tpl_auto(&bpctl_dev_arr[idx_dev]);

			}

		}
	}

	register_netdevice_notifier(&bp_notifier_block);
#ifdef BP_PROC_SUPPORT
	{
		int i = 0;
		/* unsigned long flags; */
		/* rcu_read_lock(); */
		bp_proc_create();
		for (i = 0; i < device_num; i++) {
			if (bpctl_dev_arr[i].ifindex) {
				/* spin_lock_irqsave(&bpvm_lock, flags); */
				bypass_proc_remove_dev_sd(&bpctl_dev_arr[i]);
				bypass_proc_create_dev_sd(&bpctl_dev_arr[i]);
				/* spin_unlock_irqrestore(&bpvm_lock, flags); */
			}

		}
		/* rcu_read_unlock(); */
	}
#endif

	return 0;
}

/*
* Cleanup - unregister the appropriate file from /proc
*/
static void __exit bypass_cleanup_module(void)
{
	int i;
	unregister_netdevice_notifier(&bp_notifier_block);

	for (i = 0; i < device_num; i++) {
		/* unsigned long flags; */
#ifdef BP_PROC_SUPPORT
/*	spin_lock_irqsave(&bpvm_lock, flags);
	rcu_read_lock(); */
		bypass_proc_remove_dev_sd(&bpctl_dev_arr[i]);
/*	spin_unlock_irqrestore(&bpvm_lock, flags);
	rcu_read_unlock(); */
#endif
		remove_bypass_wd_auto(&bpctl_dev_arr[i]);
		bpctl_dev_arr[i].reset_time = 0;

		remove_bypass_tpl_auto(&bpctl_dev_arr[i]);
	}

	/* unmap all devices */
	for (i = 0; i < device_num; i++) {
#ifdef BP_SELF_TEST
		kfree(bpctl_dev_arr[i].bp_tx_data);
#endif
		iounmap((void *)(bpctl_dev_arr[i].mem_map));
	}

	/* free all devices space */
	kfree(bpctl_dev_arr);

/*
* Unregister the device
*/
	unregister_chrdev(major_num, DEVICE_NAME);
}

module_init(bypass_init_module);
module_exit(bypass_cleanup_module);

int is_bypass_sd(int ifindex)
{
	return is_bypass(get_dev_idx_p(ifindex));
}

int set_bypass_sd(int ifindex, int bypass_mode)
{

	return set_bypass_fn(get_dev_idx_p(ifindex), bypass_mode);
}

int get_bypass_sd(int ifindex)
{

	return get_bypass_fn(get_dev_idx_p(ifindex));
}

int get_bypass_change_sd(int ifindex)
{

	return get_bypass_change_fn(get_dev_idx_p(ifindex));
}

int set_dis_bypass_sd(int ifindex, int dis_param)
{
	return set_dis_bypass_fn(get_dev_idx_p(ifindex), dis_param);
}

int get_dis_bypass_sd(int ifindex)
{

	return get_dis_bypass_fn(get_dev_idx_p(ifindex));
}

int set_bypass_pwoff_sd(int ifindex, int bypass_mode)
{
	return set_bypass_pwoff_fn(get_dev_idx_p(ifindex), bypass_mode);

}

int get_bypass_pwoff_sd(int ifindex)
{
	return get_bypass_pwoff_fn(get_dev_idx_p(ifindex));

}

int set_bypass_pwup_sd(int ifindex, int bypass_mode)
{
	return set_bypass_pwup_fn(get_dev_idx_p(ifindex), bypass_mode);

}

int get_bypass_pwup_sd(int ifindex)
{
	return get_bypass_pwup_fn(get_dev_idx_p(ifindex));

}

int set_bypass_wd_sd(int if_index, int ms_timeout, int *ms_timeout_set)
{
	if ((is_bypass(get_dev_idx_p(if_index))) <= 0)
		return BP_NOT_CAP;
	*ms_timeout_set = set_bypass_wd_fn(get_dev_idx_p(if_index), ms_timeout);
	return 0;
}

int get_bypass_wd_sd(int ifindex, int *timeout)
{
	return get_bypass_wd_fn(get_dev_idx_p(ifindex), timeout);

}

int get_wd_expire_time_sd(int ifindex, int *time_left)
{
	return get_wd_expire_time_fn(get_dev_idx_p(ifindex), time_left);
}

int reset_bypass_wd_timer_sd(int ifindex)
{
	return reset_bypass_wd_timer_fn(get_dev_idx_p(ifindex));

}

int get_wd_set_caps_sd(int ifindex)
{
	return get_wd_set_caps_fn(get_dev_idx_p(ifindex));

}

int set_std_nic_sd(int ifindex, int nic_mode)
{
	return set_std_nic_fn(get_dev_idx_p(ifindex), nic_mode);

}

int get_std_nic_sd(int ifindex)
{
	return get_std_nic_fn(get_dev_idx_p(ifindex));

}

int set_tap_sd(int ifindex, int tap_mode)
{
	return set_tap_fn(get_dev_idx_p(ifindex), tap_mode);

}

int get_tap_sd(int ifindex)
{
	return get_tap_fn(get_dev_idx_p(ifindex));

}

int set_tap_pwup_sd(int ifindex, int tap_mode)
{
	return set_tap_pwup_fn(get_dev_idx_p(ifindex), tap_mode);

}

int get_tap_pwup_sd(int ifindex)
{
	return get_tap_pwup_fn(get_dev_idx_p(ifindex));

}

int get_tap_change_sd(int ifindex)
{
	return get_tap_change_fn(get_dev_idx_p(ifindex));

}

int set_dis_tap_sd(int ifindex, int dis_param)
{
	return set_dis_tap_fn(get_dev_idx_p(ifindex), dis_param);

}

int get_dis_tap_sd(int ifindex)
{
	return get_dis_tap_fn(get_dev_idx_p(ifindex));

}

int set_bp_disc_sd(int ifindex, int disc_mode)
{
	return set_disc_fn(get_dev_idx_p(ifindex), disc_mode);

}

int get_bp_disc_sd(int ifindex)
{
	return get_disc_fn(get_dev_idx_p(ifindex));

}

int set_bp_disc_pwup_sd(int ifindex, int disc_mode)
{
	return set_disc_pwup_fn(get_dev_idx_p(ifindex), disc_mode);

}

int get_bp_disc_pwup_sd(int ifindex)
{
	return get_disc_pwup_fn(get_dev_idx_p(ifindex));

}

int get_bp_disc_change_sd(int ifindex)
{
	return get_disc_change_fn(get_dev_idx_p(ifindex));

}

int set_bp_dis_disc_sd(int ifindex, int dis_param)
{
	return set_dis_disc_fn(get_dev_idx_p(ifindex), dis_param);

}

int get_bp_dis_disc_sd(int ifindex)
{
	return get_dis_disc_fn(get_dev_idx_p(ifindex));

}

int get_wd_exp_mode_sd(int ifindex)
{
	return get_wd_exp_mode_fn(get_dev_idx_p(ifindex));
}

int set_wd_exp_mode_sd(int ifindex, int param)
{
	return set_wd_exp_mode_fn(get_dev_idx_p(ifindex), param);

}

int reset_cont_sd(int ifindex)
{
	return reset_cont_fn(get_dev_idx_p(ifindex));

}

int set_tx_sd(int ifindex, int tx_state)
{
	return set_tx_fn(get_dev_idx_p(ifindex), tx_state);

}

int set_tpl_sd(int ifindex, int tpl_state)
{
	return set_tpl_fn(get_dev_idx_p(ifindex), tpl_state);

}

int set_bp_hw_reset_sd(int ifindex, int status)
{
	return set_bp_hw_reset_fn(get_dev_idx_p(ifindex), status);

}

int set_wd_autoreset_sd(int ifindex, int param)
{
	return set_wd_autoreset_fn(get_dev_idx_p(ifindex), param);

}

int get_wd_autoreset_sd(int ifindex)
{
	return get_wd_autoreset_fn(get_dev_idx_p(ifindex));

}

int get_bypass_caps_sd(int ifindex)
{
	return get_bypass_caps_fn(get_dev_idx_p(ifindex));
}

int get_bypass_slave_sd(int ifindex)
{
	bpctl_dev_t *pbpctl_dev_out;
	int ret = get_bypass_slave_fn(get_dev_idx_p(ifindex), &pbpctl_dev_out);
	if (ret == 1)
		return pbpctl_dev_out->ifindex;
	return -1;

}

int get_tx_sd(int ifindex)
{
	return get_tx_fn(get_dev_idx_p(ifindex));

}

int get_tpl_sd(int ifindex)
{
	return get_tpl_fn(get_dev_idx_p(ifindex));

}

int get_bp_hw_reset_sd(int ifindex)
{
	return get_bp_hw_reset_fn(get_dev_idx_p(ifindex));

}

int get_bypass_info_sd(int ifindex, struct bp_info *bp_info)
{
	return get_bypass_info_fn(get_dev_idx_p(ifindex), bp_info->prod_name, &bp_info->fw_ver);
}

int bp_if_scan_sd(void)
{
	if_scan_init();
	return 0;
}

EXPORT_SYMBOL_NOVERS(is_bypass_sd);
EXPORT_SYMBOL_NOVERS(get_bypass_slave_sd);
EXPORT_SYMBOL_NOVERS(get_bypass_caps_sd);
EXPORT_SYMBOL_NOVERS(get_wd_set_caps_sd);
EXPORT_SYMBOL_NOVERS(set_bypass_sd);
EXPORT_SYMBOL_NOVERS(get_bypass_sd);
EXPORT_SYMBOL_NOVERS(get_bypass_change_sd);
EXPORT_SYMBOL_NOVERS(set_dis_bypass_sd);
EXPORT_SYMBOL_NOVERS(get_dis_bypass_sd);
EXPORT_SYMBOL_NOVERS(set_bypass_pwoff_sd);
EXPORT_SYMBOL_NOVERS(get_bypass_pwoff_sd);
EXPORT_SYMBOL_NOVERS(set_bypass_pwup_sd);
EXPORT_SYMBOL_NOVERS(get_bypass_pwup_sd);
EXPORT_SYMBOL_NOVERS(set_bypass_wd_sd);
EXPORT_SYMBOL_NOVERS(get_bypass_wd_sd);
EXPORT_SYMBOL_NOVERS(get_wd_expire_time_sd);
EXPORT_SYMBOL_NOVERS(reset_bypass_wd_timer_sd);
EXPORT_SYMBOL_NOVERS(set_std_nic_sd);
EXPORT_SYMBOL_NOVERS(get_std_nic_sd);
EXPORT_SYMBOL_NOVERS(set_tx_sd);
EXPORT_SYMBOL_NOVERS(get_tx_sd);
EXPORT_SYMBOL_NOVERS(set_tpl_sd);
EXPORT_SYMBOL_NOVERS(get_tpl_sd);
EXPORT_SYMBOL_NOVERS(set_bp_hw_reset_sd);
EXPORT_SYMBOL_NOVERS(get_bp_hw_reset_sd);
EXPORT_SYMBOL_NOVERS(set_tap_sd);
EXPORT_SYMBOL_NOVERS(get_tap_sd);
EXPORT_SYMBOL_NOVERS(get_tap_change_sd);
EXPORT_SYMBOL_NOVERS(set_dis_tap_sd);
EXPORT_SYMBOL_NOVERS(get_dis_tap_sd);
EXPORT_SYMBOL_NOVERS(set_tap_pwup_sd);
EXPORT_SYMBOL_NOVERS(get_tap_pwup_sd);
EXPORT_SYMBOL_NOVERS(set_wd_exp_mode_sd);
EXPORT_SYMBOL_NOVERS(get_wd_exp_mode_sd);
EXPORT_SYMBOL_NOVERS(set_wd_autoreset_sd);
EXPORT_SYMBOL_NOVERS(get_wd_autoreset_sd);
EXPORT_SYMBOL_NOVERS(set_bp_disc_sd);
EXPORT_SYMBOL_NOVERS(get_bp_disc_sd);
EXPORT_SYMBOL_NOVERS(get_bp_disc_change_sd);
EXPORT_SYMBOL_NOVERS(set_bp_dis_disc_sd);
EXPORT_SYMBOL_NOVERS(get_bp_dis_disc_sd);
EXPORT_SYMBOL_NOVERS(set_bp_disc_pwup_sd);
EXPORT_SYMBOL_NOVERS(get_bp_disc_pwup_sd);
EXPORT_SYMBOL_NOVERS(get_bypass_info_sd);
EXPORT_SYMBOL_NOVERS(bp_if_scan_sd);

#define BP_PROC_DIR "bypass"

#define GPIO6_SET_ENTRY_SD           "gpio6_set"
#define GPIO6_CLEAR_ENTRY_SD         "gpio6_clear"

#define GPIO7_SET_ENTRY_SD           "gpio7_set"
#define GPIO7_CLEAR_ENTRY_SD         "gpio7_clear"

#define PULSE_SET_ENTRY_SD            "pulse_set"
#define ZERO_SET_ENTRY_SD            "zero_set"
#define PULSE_GET1_ENTRY_SD            "pulse_get1"
#define PULSE_GET2_ENTRY_SD            "pulse_get2"

#define CMND_ON_ENTRY_SD              "cmnd_on"
#define CMND_OFF_ENTRY_SD             "cmnd_off"
#define RESET_CONT_ENTRY_SD           "reset_cont"

 /*COMMANDS*/
#define BYPASS_INFO_ENTRY_SD     "bypass_info"
#define BYPASS_SLAVE_ENTRY_SD    "bypass_slave"
#define BYPASS_CAPS_ENTRY_SD     "bypass_caps"
#define WD_SET_CAPS_ENTRY_SD     "wd_set_caps"
#define BYPASS_ENTRY_SD          "bypass"
#define BYPASS_CHANGE_ENTRY_SD   "bypass_change"
#define BYPASS_WD_ENTRY_SD       "bypass_wd"
#define WD_EXPIRE_TIME_ENTRY_SD  "wd_expire_time"
#define RESET_BYPASS_WD_ENTRY_SD "reset_bypass_wd"
#define DIS_BYPASS_ENTRY_SD      "dis_bypass"
#define BYPASS_PWUP_ENTRY_SD     "bypass_pwup"
#define BYPASS_PWOFF_ENTRY_SD     "bypass_pwoff"
#define STD_NIC_ENTRY_SD         "std_nic"
#define STD_NIC_ENTRY_SD         "std_nic"
#define TAP_ENTRY_SD             "tap"
#define TAP_CHANGE_ENTRY_SD      "tap_change"
#define DIS_TAP_ENTRY_SD         "dis_tap"
#define TAP_PWUP_ENTRY_SD        "tap_pwup"
#define TWO_PORT_LINK_ENTRY_SD   "two_port_link"
#define WD_EXP_MODE_ENTRY_SD     "wd_exp_mode"
#define WD_AUTORESET_ENTRY_SD    "wd_autoreset"
#define TPL_ENTRY_SD             "tpl"
#define WAIT_AT_PWUP_ENTRY_SD    "wait_at_pwup"
#define HW_RESET_ENTRY_SD        "hw_reset"
#define DISC_ENTRY_SD             "disc"
#define DISC_CHANGE_ENTRY_SD      "disc_change"
#define DIS_DISC_ENTRY_SD         "dis_disc"
#define DISC_PWUP_ENTRY_SD        "disc_pwup"
static struct proc_dir_entry *bp_procfs_dir;

static struct proc_dir_entry *proc_getdir(char *name,
					  struct proc_dir_entry *proc_dir)
{
	struct proc_dir_entry *pde = proc_dir;

	for (pde = pde->subdir; pde; pde = pde->next) {
		if (pde->namelen && (strcmp(name, pde->name) == 0)) {
			/* directory exists */
			break;
		}
	}
	if (pde == (struct proc_dir_entry *)0) {
		/* create the directory */
		pde = proc_mkdir(name, proc_dir);
		if (pde == (struct proc_dir_entry *)0) {

			return pde;
		}
	}

	return pde;
}

int bp_proc_create(void)
{
	bp_procfs_dir = proc_getdir(BP_PROC_DIR, init_net.proc_net);
	if (bp_procfs_dir == (struct proc_dir_entry *)0) {
		printk(KERN_DEBUG
		       "Could not create procfs nicinfo directory %s\n",
		       BP_PROC_DIR);
		return -1;
	}
	return 0;
}

int
bypass_proc_create_entry_sd(struct pfs_unit_sd *pfs_unit_curr,
			    char *proc_name,
			    write_proc_t *write_proc,
			    read_proc_t *read_proc,
			    struct proc_dir_entry *parent_pfs, void *data)
{
	strcpy(pfs_unit_curr->proc_name, proc_name);
	pfs_unit_curr->proc_entry = create_proc_entry(pfs_unit_curr->proc_name,
						      S_IFREG | S_IRUSR |
						      S_IWUSR | S_IRGRP |
						      S_IROTH, parent_pfs);
	if (pfs_unit_curr->proc_entry == NULL)
		return -1;

	pfs_unit_curr->proc_entry->read_proc = read_proc;
	pfs_unit_curr->proc_entry->write_proc = write_proc;
	pfs_unit_curr->proc_entry->data = data;

	return 0;

}

int
get_bypass_info_pfs(char *page, char **start, off_t off, int count,
		    int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;
	int len = 0;

	len += sprintf(page, "Name\t\t\t%s\n", pbp_device_block->name);
	len +=
	    sprintf(page + len, "Firmware version\t0x%x\n",
		    pbp_device_block->bp_fw_ver);

	*eof = 1;
	return len;
}

int
get_bypass_slave_pfs(char *page, char **start, off_t off, int count,
		     int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0;
	bpctl_dev_t *pbp_device_block_slave = NULL;
	int idx_dev = 0;
	struct net_device *net_slave_dev = NULL;

	if ((pbp_device_block->func == 0) || (pbp_device_block->func == 2)) {
		for (idx_dev = 0;
		     ((bpctl_dev_arr[idx_dev].pdev != NULL)
		      && (idx_dev < device_num)); idx_dev++) {
			if ((bpctl_dev_arr[idx_dev].bus ==
			     pbp_device_block->bus)
			    && (bpctl_dev_arr[idx_dev].slot ==
				pbp_device_block->slot)) {
				if ((pbp_device_block->func == 0)
				    && (bpctl_dev_arr[idx_dev].func == 1)) {
					pbp_device_block_slave =
					    &bpctl_dev_arr[idx_dev];
					break;
				}
				if ((pbp_device_block->func == 2) &&
				    (bpctl_dev_arr[idx_dev].func == 3)) {
					pbp_device_block_slave =
					    &bpctl_dev_arr[idx_dev];
					break;
				}
			}
		}
	} else
		pbp_device_block_slave = pbp_device_block;
	if (!pbp_device_block_slave) {
		len = sprintf(page, "fail\n");
		*eof = 1;
		return len;
	}
	net_slave_dev = pbp_device_block_slave->ndev;
	if (net_slave_dev)
		len = sprintf(page, "%s\n", net_slave_dev->name);

	*eof = 1;
	return len;
}

int
get_bypass_caps_pfs(char *page, char **start, off_t off, int count,
		    int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0, ret = 0;

	ret = get_bypass_caps_fn(pbp_device_block);
	if (ret == BP_NOT_CAP)
		len = sprintf(page, "-1\n");
	else
		len = sprintf(page, "0x%x\n", ret);
	*eof = 1;
	return len;

}

int
get_wd_set_caps_pfs(char *page, char **start, off_t off, int count,
		    int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0, ret = 0;

	ret = get_wd_set_caps_fn(pbp_device_block);
	if (ret == BP_NOT_CAP)
		len = sprintf(page, "-1\n");
	else
		len = sprintf(page, "0x%x\n", ret);
	*eof = 1;
	return len;
}

int
set_bypass_pfs(struct file *file, const char *buffer,
	       unsigned long count, void *data)
{

	char kbuf[256];
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int bypass_param = 0, length = 0;

	if (count > (sizeof(kbuf) - 1))
		return -1;

	if (copy_from_user(&kbuf, buffer, count)) {
		return -1;
	}

	kbuf[count] = '\0';
	length = strlen(kbuf);
	if (kbuf[length - 1] == '\n')
		kbuf[--length] = '\0';

	if (strcmp(kbuf, "on") == 0)
		bypass_param = 1;
	else if (strcmp(kbuf, "off") == 0)
		bypass_param = 0;

	set_bypass_fn(pbp_device_block, bypass_param);

	return count;
}

int
set_tap_pfs(struct file *file, const char *buffer,
	    unsigned long count, void *data)
{

	char kbuf[256];
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int tap_param = 0, length = 0;

	if (count > (sizeof(kbuf) - 1))
		return -1;

	if (copy_from_user(&kbuf, buffer, count)) {
		return -1;
	}

	kbuf[count] = '\0';
	length = strlen(kbuf);
	if (kbuf[length - 1] == '\n')
		kbuf[--length] = '\0';

	if (strcmp(kbuf, "on") == 0)
		tap_param = 1;
	else if (strcmp(kbuf, "off") == 0)
		tap_param = 0;

	set_tap_fn(pbp_device_block, tap_param);

	return count;
}

int
set_disc_pfs(struct file *file, const char *buffer,
	     unsigned long count, void *data)
{

	char kbuf[256];
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int tap_param = 0, length = 0;

	if (count > (sizeof(kbuf) - 1))
		return -1;

	if (copy_from_user(&kbuf, buffer, count)) {
		return -1;
	}

	kbuf[count] = '\0';
	length = strlen(kbuf);
	if (kbuf[length - 1] == '\n')
		kbuf[--length] = '\0';

	if (strcmp(kbuf, "on") == 0)
		tap_param = 1;
	else if (strcmp(kbuf, "off") == 0)
		tap_param = 0;

	set_disc_fn(pbp_device_block, tap_param);

	return count;
}

int
get_bypass_pfs(char *page, char **start, off_t off, int count,
	       int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0, ret = 0;

	ret = get_bypass_fn(pbp_device_block);
	if (ret == BP_NOT_CAP)
		len = sprintf(page, "fail\n");
	else if (ret == 1)
		len = sprintf(page, "on\n");
	else if (ret == 0)
		len = sprintf(page, "off\n");

	*eof = 1;
	return len;
}

int
get_tap_pfs(char *page, char **start, off_t off, int count,
	    int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0, ret = 0;

	ret = get_tap_fn(pbp_device_block);
	if (ret == BP_NOT_CAP)
		len = sprintf(page, "fail\n");
	else if (ret == 1)
		len = sprintf(page, "on\n");
	else if (ret == 0)
		len = sprintf(page, "off\n");

	*eof = 1;
	return len;
}

int
get_disc_pfs(char *page, char **start, off_t off, int count,
	     int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0, ret = 0;

	ret = get_disc_fn(pbp_device_block);
	if (ret == BP_NOT_CAP)
		len = sprintf(page, "fail\n");
	else if (ret == 1)
		len = sprintf(page, "on\n");
	else if (ret == 0)
		len = sprintf(page, "off\n");

	*eof = 1;
	return len;
}

int
get_bypass_change_pfs(char *page, char **start, off_t off, int count,
		      int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0, ret = 0;

	ret = get_bypass_change_fn(pbp_device_block);
	if (ret == 1)
		len = sprintf(page, "on\n");
	else if (ret == 0)
		len = sprintf(page, "off\n");
	else
		len = sprintf(page, "fail\n");

	*eof = 1;
	return len;
}

int
get_tap_change_pfs(char *page, char **start, off_t off, int count,
		   int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0, ret = 0;

	ret = get_tap_change_fn(pbp_device_block);
	if (ret == 1)
		len = sprintf(page, "on\n");
	else if (ret == 0)
		len = sprintf(page, "off\n");
	else
		len = sprintf(page, "fail\n");

	*eof = 1;
	return len;
}

int
get_disc_change_pfs(char *page, char **start, off_t off, int count,
		    int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0, ret = 0;

	ret = get_disc_change_fn(pbp_device_block);
	if (ret == 1)
		len = sprintf(page, "on\n");
	else if (ret == 0)
		len = sprintf(page, "off\n");
	else
		len = sprintf(page, "fail\n");

	*eof = 1;
	return len;
}

#define isdigit(c) (c >= '0' && c <= '9')
__inline static int atoi(char **s)
{
	int i = 0;
	while (isdigit(**s))
		i = i * 10 + *((*s)++) - '0';
	return i;
}

int
set_bypass_wd_pfs(struct file *file, const char *buffer,
		  unsigned long count, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;
	int timeout;
	int ret;

	ret = kstrtoint_from_user(buffer, count, 10, &timeout);
	if (ret)
		return ret;
	set_bypass_wd_fn(pbp_device_block, timeout);

	return count;
}

int
get_bypass_wd_pfs(char *page, char **start, off_t off, int count,
		  int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0, ret = 0, timeout = 0;

	ret = get_bypass_wd_fn(pbp_device_block, &timeout);
	if (ret == BP_NOT_CAP)
		len = sprintf(page, "fail\n");
	else if (timeout == -1)
		len = sprintf(page, "unknown\n");
	else if (timeout == 0)
		len = sprintf(page, "disable\n");
	else
		len = sprintf(page, "%d\n", timeout);

	*eof = 1;
	return len;
}

int
get_wd_expire_time_pfs(char *page, char **start, off_t off, int count,
		       int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0, ret = 0, timeout = 0;

	ret = get_wd_expire_time_fn(pbp_device_block, &timeout);
	if (ret == BP_NOT_CAP)
		len = sprintf(page, "fail\n");
	else if (timeout == -1)
		len = sprintf(page, "expire\n");
	else if (timeout == 0)
		len = sprintf(page, "disable\n");

	else
		len = sprintf(page, "%d\n", timeout);
	*eof = 1;
	return len;
}

int
get_tpl_pfs(char *page, char **start, off_t off, int count,
	    int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0, ret = 0;

	ret = get_tpl_fn(pbp_device_block);
	if (ret == BP_NOT_CAP)
		len = sprintf(page, "fail\n");
	else if (ret == 1)
		len = sprintf(page, "on\n");
	else if (ret == 0)
		len = sprintf(page, "off\n");

	*eof = 1;
	return len;
}

#ifdef PMC_FIX_FLAG
int
get_wait_at_pwup_pfs(char *page, char **start, off_t off, int count,
		     int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0, ret = 0;

	ret = get_bp_wait_at_pwup_fn(pbp_device_block);
	if (ret == BP_NOT_CAP)
		len = sprintf(page, "fail\n");
	else if (ret == 1)
		len = sprintf(page, "on\n");
	else if (ret == 0)
		len = sprintf(page, "off\n");

	*eof = 1;
	return len;
}

int
get_hw_reset_pfs(char *page, char **start, off_t off, int count,
		 int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0, ret = 0;

	ret = get_bp_hw_reset_fn(pbp_device_block);
	if (ret == BP_NOT_CAP)
		len = sprintf(page, "fail\n");
	else if (ret == 1)
		len = sprintf(page, "on\n");
	else if (ret == 0)
		len = sprintf(page, "off\n");

	*eof = 1;
	return len;
}

#endif				/*PMC_WAIT_FLAG */

int
reset_bypass_wd_pfs(char *page, char **start, off_t off, int count,
		    int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0, ret = 0;

	ret = reset_bypass_wd_timer_fn(pbp_device_block);
	if (ret == BP_NOT_CAP)
		len = sprintf(page, "fail\n");
	else if (ret == 0)
		len = sprintf(page, "disable\n");
	else if (ret == 1)
		len = sprintf(page, "success\n");

	*eof = 1;
	return len;
}

int
set_dis_bypass_pfs(struct file *file, const char *buffer,
		   unsigned long count, void *data)
{

	char kbuf[256];
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int bypass_param = 0, length = 0;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(&kbuf, buffer, count)) {
		return -1;
	}

	kbuf[count] = '\0';
	length = strlen(kbuf);
	if (kbuf[length - 1] == '\n')
		kbuf[--length] = '\0';

	if (strcmp(kbuf, "on") == 0)
		bypass_param = 1;
	else if (strcmp(kbuf, "off") == 0)
		bypass_param = 0;

	set_dis_bypass_fn(pbp_device_block, bypass_param);

	return count;
}

int
set_dis_tap_pfs(struct file *file, const char *buffer,
		unsigned long count, void *data)
{

	char kbuf[256];
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int tap_param = 0, length = 0;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(&kbuf, buffer, count)) {
		return -1;
	}

	kbuf[count] = '\0';
	length = strlen(kbuf);
	if (kbuf[length - 1] == '\n')
		kbuf[--length] = '\0';

	if (strcmp(kbuf, "on") == 0)
		tap_param = 1;
	else if (strcmp(kbuf, "off") == 0)
		tap_param = 0;

	set_dis_tap_fn(pbp_device_block, tap_param);

	return count;
}

int
set_dis_disc_pfs(struct file *file, const char *buffer,
		 unsigned long count, void *data)
{

	char kbuf[256];
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int tap_param = 0, length = 0;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(&kbuf, buffer, count)) {
		return -1;
	}

	kbuf[count] = '\0';
	length = strlen(kbuf);
	if (kbuf[length - 1] == '\n')
		kbuf[--length] = '\0';

	if (strcmp(kbuf, "on") == 0)
		tap_param = 1;
	else if (strcmp(kbuf, "off") == 0)
		tap_param = 0;

	set_dis_disc_fn(pbp_device_block, tap_param);

	return count;
}

int
get_dis_bypass_pfs(char *page, char **start, off_t off, int count,
		   int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0, ret = 0;

	ret = get_dis_bypass_fn(pbp_device_block);
	if (ret == BP_NOT_CAP)
		len = sprintf(page, "fail\n");
	else if (ret == 0)
		len = sprintf(page, "off\n");
	else
		len = sprintf(page, "on\n");

	*eof = 1;
	return len;
}

int
get_dis_tap_pfs(char *page, char **start, off_t off, int count,
		int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0, ret = 0;

	ret = get_dis_tap_fn(pbp_device_block);
	if (ret == BP_NOT_CAP)
		len = sprintf(page, "fail\n");
	else if (ret == 0)
		len = sprintf(page, "off\n");
	else
		len = sprintf(page, "on\n");

	*eof = 1;
	return len;
}

int
get_dis_disc_pfs(char *page, char **start, off_t off, int count,
		 int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0, ret = 0;

	ret = get_dis_disc_fn(pbp_device_block);
	if (ret == BP_NOT_CAP)
		len = sprintf(page, "fail\n");
	else if (ret == 0)
		len = sprintf(page, "off\n");
	else
		len = sprintf(page, "on\n");

	*eof = 1;
	return len;
}

int
set_bypass_pwup_pfs(struct file *file, const char *buffer,
		    unsigned long count, void *data)
{

	char kbuf[256];
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int bypass_param = 0, length = 0;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(&kbuf, buffer, count)) {
		return -1;
	}

	kbuf[count] = '\0';
	length = strlen(kbuf);
	if (kbuf[length - 1] == '\n')
		kbuf[--length] = '\0';

	if (strcmp(kbuf, "on") == 0)
		bypass_param = 1;
	else if (strcmp(kbuf, "off") == 0)
		bypass_param = 0;

	set_bypass_pwup_fn(pbp_device_block, bypass_param);

	return count;
}

int
set_bypass_pwoff_pfs(struct file *file, const char *buffer,
		     unsigned long count, void *data)
{

	char kbuf[256];
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int bypass_param = 0, length = 0;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(&kbuf, buffer, count)) {
		return -1;
	}

	kbuf[count] = '\0';
	length = strlen(kbuf);
	if (kbuf[length - 1] == '\n')
		kbuf[--length] = '\0';

	if (strcmp(kbuf, "on") == 0)
		bypass_param = 1;
	else if (strcmp(kbuf, "off") == 0)
		bypass_param = 0;

	set_bypass_pwoff_fn(pbp_device_block, bypass_param);

	return count;
}

int
set_tap_pwup_pfs(struct file *file, const char *buffer,
		 unsigned long count, void *data)
{

	char kbuf[256];
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int tap_param = 0, length = 0;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(&kbuf, buffer, count)) {
		return -1;
	}

	kbuf[count] = '\0';
	length = strlen(kbuf);
	if (kbuf[length - 1] == '\n')
		kbuf[--length] = '\0';

	if (strcmp(kbuf, "on") == 0)
		tap_param = 1;
	else if (strcmp(kbuf, "off") == 0)
		tap_param = 0;

	set_tap_pwup_fn(pbp_device_block, tap_param);

	return count;
}

int
set_disc_pwup_pfs(struct file *file, const char *buffer,
		  unsigned long count, void *data)
{

	char kbuf[256];
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int tap_param = 0, length = 0;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(&kbuf, buffer, count)) {
		return -1;
	}

	kbuf[count] = '\0';
	length = strlen(kbuf);
	if (kbuf[length - 1] == '\n')
		kbuf[--length] = '\0';

	if (strcmp(kbuf, "on") == 0)
		tap_param = 1;
	else if (strcmp(kbuf, "off") == 0)
		tap_param = 0;

	set_disc_pwup_fn(pbp_device_block, tap_param);

	return count;
}

int
get_bypass_pwup_pfs(char *page, char **start, off_t off, int count,
		    int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0, ret = 0;

	ret = get_bypass_pwup_fn(pbp_device_block);
	if (ret == BP_NOT_CAP)
		len = sprintf(page, "fail\n");
	else if (ret == 0)
		len = sprintf(page, "off\n");
	else
		len = sprintf(page, "on\n");

	*eof = 1;
	return len;
}

int
get_bypass_pwoff_pfs(char *page, char **start, off_t off, int count,
		     int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0, ret = 0;

	ret = get_bypass_pwoff_fn(pbp_device_block);
	if (ret == BP_NOT_CAP)
		len = sprintf(page, "fail\n");
	else if (ret == 0)
		len = sprintf(page, "off\n");
	else
		len = sprintf(page, "on\n");

	*eof = 1;
	return len;
}

int
get_tap_pwup_pfs(char *page, char **start, off_t off, int count,
		 int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0, ret = 0;

	ret = get_tap_pwup_fn(pbp_device_block);
	if (ret == BP_NOT_CAP)
		len = sprintf(page, "fail\n");
	else if (ret == 0)
		len = sprintf(page, "off\n");
	else
		len = sprintf(page, "on\n");

	*eof = 1;
	return len;
}

int
get_disc_pwup_pfs(char *page, char **start, off_t off, int count,
		  int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0, ret = 0;

	ret = get_disc_pwup_fn(pbp_device_block);
	if (ret == BP_NOT_CAP)
		len = sprintf(page, "fail\n");
	else if (ret == 0)
		len = sprintf(page, "off\n");
	else
		len = sprintf(page, "on\n");

	*eof = 1;
	return len;
}

int
set_std_nic_pfs(struct file *file, const char *buffer,
		unsigned long count, void *data)
{

	char kbuf[256];
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int bypass_param = 0, length = 0;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(&kbuf, buffer, count)) {
		return -1;
	}

	kbuf[count] = '\0';
	length = strlen(kbuf);
	if (kbuf[length - 1] == '\n')
		kbuf[--length] = '\0';

	if (strcmp(kbuf, "on") == 0)
		bypass_param = 1;
	else if (strcmp(kbuf, "off") == 0)
		bypass_param = 0;

	set_std_nic_fn(pbp_device_block, bypass_param);

	return count;
}

int
get_std_nic_pfs(char *page, char **start, off_t off, int count,
		int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0, ret = 0;

	ret = get_std_nic_fn(pbp_device_block);
	if (ret == BP_NOT_CAP)
		len = sprintf(page, "fail\n");
	else if (ret == 0)
		len = sprintf(page, "off\n");
	else
		len = sprintf(page, "on\n");

	*eof = 1;
	return len;
}

int
get_wd_exp_mode_pfs(char *page, char **start, off_t off, int count,
		    int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0, ret = 0;

	ret = get_wd_exp_mode_fn(pbp_device_block);
	if (ret == 1)
		len = sprintf(page, "tap\n");
	else if (ret == 0)
		len = sprintf(page, "bypass\n");
	else if (ret == 2)
		len = sprintf(page, "disc\n");

	else
		len = sprintf(page, "fail\n");

	*eof = 1;
	return len;
}

int
set_wd_exp_mode_pfs(struct file *file, const char *buffer,
		    unsigned long count, void *data)
{

	char kbuf[256];
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int bypass_param = 0, length = 0;

	if (count > (sizeof(kbuf) - 1))
		return -1;

	if (copy_from_user(&kbuf, buffer, count)) {
		return -1;
	}

	kbuf[count] = '\0';
	length = strlen(kbuf);
	if (kbuf[length - 1] == '\n')
		kbuf[--length] = '\0';

	if (strcmp(kbuf, "tap") == 0)
		bypass_param = 1;
	else if (strcmp(kbuf, "bypass") == 0)
		bypass_param = 0;
	else if (strcmp(kbuf, "disc") == 0)
		bypass_param = 2;

	set_wd_exp_mode_fn(pbp_device_block, bypass_param);

	return count;
}

int
get_wd_autoreset_pfs(char *page, char **start, off_t off, int count,
		     int *eof, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int len = 0, ret = 0;

	ret = get_wd_autoreset_fn(pbp_device_block);
	if (ret >= 0)
		len = sprintf(page, "%d\n", ret);
	else
		len = sprintf(page, "fail\n");

	*eof = 1;
	return len;
}

int
set_wd_autoreset_pfs(struct file *file, const char *buffer,
		     unsigned long count, void *data)
{
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;
	int timeout;
	int ret;

	ret = kstrtoint_from_user(buffer, count, 10, &timeout);
	if (ret)
		return ret;
	set_wd_autoreset_fn(pbp_device_block, timeout);

	return count;
}

int
set_tpl_pfs(struct file *file, const char *buffer,
	    unsigned long count, void *data)
{

	char kbuf[256];
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int tpl_param = 0, length = 0;

	if (count > (sizeof(kbuf) - 1))
		return -1;

	if (copy_from_user(&kbuf, buffer, count)) {
		return -1;
	}

	kbuf[count] = '\0';
	length = strlen(kbuf);
	if (kbuf[length - 1] == '\n')
		kbuf[--length] = '\0';

	if (strcmp(kbuf, "on") == 0)
		tpl_param = 1;
	else if (strcmp(kbuf, "off") == 0)
		tpl_param = 0;

	set_tpl_fn(pbp_device_block, tpl_param);

	return count;
}

#ifdef PMC_FIX_FLAG
int
set_wait_at_pwup_pfs(struct file *file, const char *buffer,
		     unsigned long count, void *data)
{

	char kbuf[256];
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int tpl_param = 0, length = 0;

	if (count > (sizeof(kbuf) - 1))
		return -1;

	if (copy_from_user(&kbuf, buffer, count)) {
		return -1;
	}

	kbuf[count] = '\0';
	length = strlen(kbuf);
	if (kbuf[length - 1] == '\n')
		kbuf[--length] = '\0';

	if (strcmp(kbuf, "on") == 0)
		tpl_param = 1;
	else if (strcmp(kbuf, "off") == 0)
		tpl_param = 0;

	set_bp_wait_at_pwup_fn(pbp_device_block, tpl_param);

	return count;
}

int
set_hw_reset_pfs(struct file *file, const char *buffer,
		 unsigned long count, void *data)
{

	char kbuf[256];
	bpctl_dev_t *pbp_device_block = (bpctl_dev_t *) data;

	int tpl_param = 0, length = 0;

	if (count > (sizeof(kbuf) - 1))
		return -1;

	if (copy_from_user(&kbuf, buffer, count)) {
		return -1;
	}

	kbuf[count] = '\0';
	length = strlen(kbuf);
	if (kbuf[length - 1] == '\n')
		kbuf[--length] = '\0';

	if (strcmp(kbuf, "on") == 0)
		tpl_param = 1;
	else if (strcmp(kbuf, "off") == 0)
		tpl_param = 0;

	set_bp_hw_reset_fn(pbp_device_block, tpl_param);

	return count;
}

#endif				/*PMC_FIX_FLAG */

int bypass_proc_create_dev_sd(bpctl_dev_t *pbp_device_block)
{
	struct bypass_pfs_sd *current_pfs = &(pbp_device_block->bypass_pfs_set);
	static struct proc_dir_entry *procfs_dir = NULL;
	int ret = 0;

	if (!pbp_device_block->ndev)
		return -1;
	sprintf(current_pfs->dir_name, "bypass_%s",
		pbp_device_block->ndev->name);

	if (!bp_procfs_dir)
		return -1;

	/* create device proc dir */
	procfs_dir = proc_getdir(current_pfs->dir_name, bp_procfs_dir);
	if (procfs_dir == 0) {
		printk(KERN_DEBUG "Could not create procfs directory %s\n",
		       current_pfs->dir_name);
		return -1;
	}
	current_pfs->bypass_entry = procfs_dir;

	if (bypass_proc_create_entry_sd(&(current_pfs->bypass_info), BYPASS_INFO_ENTRY_SD, NULL,	/* write */
					get_bypass_info_pfs,	/* read  */
					procfs_dir, pbp_device_block))
		ret = -1;

	if (pbp_device_block->bp_caps & SW_CTL_CAP) {

		/* Create set param proc's */
		if (bypass_proc_create_entry_sd(&(current_pfs->bypass_slave), BYPASS_SLAVE_ENTRY_SD, NULL,	/* write */
						get_bypass_slave_pfs,	/* read  */
						procfs_dir, pbp_device_block))
			ret = -1;

		if (bypass_proc_create_entry_sd(&(current_pfs->bypass_caps), BYPASS_CAPS_ENTRY_SD, NULL,	/* write */
						get_bypass_caps_pfs,	/* read  */
						procfs_dir, pbp_device_block))
			ret = -1;

		if (bypass_proc_create_entry_sd(&(current_pfs->wd_set_caps), WD_SET_CAPS_ENTRY_SD, NULL,	/* write */
						get_wd_set_caps_pfs,	/* read  */
						procfs_dir, pbp_device_block))
			ret = -1;
		if (bypass_proc_create_entry_sd(&(current_pfs->bypass_wd), BYPASS_WD_ENTRY_SD, set_bypass_wd_pfs,	/* write */
						get_bypass_wd_pfs,	/* read  */
						procfs_dir, pbp_device_block))
			ret = -1;

		if (bypass_proc_create_entry_sd(&(current_pfs->wd_expire_time), WD_EXPIRE_TIME_ENTRY_SD, NULL,	/* write */
						get_wd_expire_time_pfs,	/* read  */
						procfs_dir, pbp_device_block))
			ret = -1;

		if (bypass_proc_create_entry_sd(&(current_pfs->reset_bypass_wd), RESET_BYPASS_WD_ENTRY_SD, NULL,	/* write */
						reset_bypass_wd_pfs,	/* read  */
						procfs_dir, pbp_device_block))
			ret = -1;

		if (bypass_proc_create_entry_sd(&(current_pfs->std_nic), STD_NIC_ENTRY_SD, set_std_nic_pfs,	/* write */
						get_std_nic_pfs,	/* read  */
						procfs_dir, pbp_device_block))
			ret = -1;

		if (pbp_device_block->bp_caps & BP_CAP) {
			if (bypass_proc_create_entry_sd(&(current_pfs->bypass), BYPASS_ENTRY_SD, set_bypass_pfs,	/* write */
							get_bypass_pfs,	/* read  */
							procfs_dir,
							pbp_device_block))
				ret = -1;

			if (bypass_proc_create_entry_sd(&(current_pfs->dis_bypass), DIS_BYPASS_ENTRY_SD, set_dis_bypass_pfs,	/* write */
							get_dis_bypass_pfs,	/* read  */
							procfs_dir,
							pbp_device_block))
				ret = -1;

			if (bypass_proc_create_entry_sd(&(current_pfs->bypass_pwup), BYPASS_PWUP_ENTRY_SD, set_bypass_pwup_pfs,	/* write */
							get_bypass_pwup_pfs,	/* read  */
							procfs_dir,
							pbp_device_block))
				ret = -1;
			if (bypass_proc_create_entry_sd(&(current_pfs->bypass_pwoff), BYPASS_PWOFF_ENTRY_SD, set_bypass_pwoff_pfs,	/* write */
							get_bypass_pwoff_pfs,	/* read  */
							procfs_dir,
							pbp_device_block))
				ret = -1;

			if (bypass_proc_create_entry_sd(&(current_pfs->bypass_change), BYPASS_CHANGE_ENTRY_SD, NULL,	/* write */
							get_bypass_change_pfs,	/* read  */
							procfs_dir,
							pbp_device_block))
				ret = -1;
		}

		if (pbp_device_block->bp_caps & TAP_CAP) {

			if (bypass_proc_create_entry_sd(&(current_pfs->tap), TAP_ENTRY_SD, set_tap_pfs,	/* write */
							get_tap_pfs,	/* read  */
							procfs_dir,
							pbp_device_block))
				ret = -1;

			if (bypass_proc_create_entry_sd(&(current_pfs->dis_tap), DIS_TAP_ENTRY_SD, set_dis_tap_pfs,	/* write */
							get_dis_tap_pfs,	/* read  */
							procfs_dir,
							pbp_device_block))
				ret = -1;

			if (bypass_proc_create_entry_sd(&(current_pfs->tap_pwup), TAP_PWUP_ENTRY_SD, set_tap_pwup_pfs,	/* write */
							get_tap_pwup_pfs,	/* read  */
							procfs_dir,
							pbp_device_block))
				ret = -1;

			if (bypass_proc_create_entry_sd(&(current_pfs->tap_change), TAP_CHANGE_ENTRY_SD, NULL,	/* write */
							get_tap_change_pfs,	/* read  */
							procfs_dir,
							pbp_device_block))
				ret = -1;
		}
		if (pbp_device_block->bp_caps & DISC_CAP) {

			if (bypass_proc_create_entry_sd(&(current_pfs->tap), DISC_ENTRY_SD, set_disc_pfs,	/* write */
							get_disc_pfs,	/* read  */
							procfs_dir,
							pbp_device_block))
				ret = -1;
#if 1

			if (bypass_proc_create_entry_sd(&(current_pfs->dis_tap), DIS_DISC_ENTRY_SD, set_dis_disc_pfs,	/* write */
							get_dis_disc_pfs,	/* read  */
							procfs_dir,
							pbp_device_block))
				ret = -1;
#endif

			if (bypass_proc_create_entry_sd(&(current_pfs->tap_pwup), DISC_PWUP_ENTRY_SD, set_disc_pwup_pfs,	/* write */
							get_disc_pwup_pfs,	/* read  */
							procfs_dir,
							pbp_device_block))
				ret = -1;

			if (bypass_proc_create_entry_sd(&(current_pfs->tap_change), DISC_CHANGE_ENTRY_SD, NULL,	/* write */
							get_disc_change_pfs,	/* read  */
							procfs_dir,
							pbp_device_block))
				ret = -1;
		}

		if (bypass_proc_create_entry_sd(&(current_pfs->wd_exp_mode), WD_EXP_MODE_ENTRY_SD, set_wd_exp_mode_pfs,	/* write */
						get_wd_exp_mode_pfs,	/* read  */
						procfs_dir, pbp_device_block))
			ret = -1;

		if (bypass_proc_create_entry_sd(&(current_pfs->wd_autoreset), WD_AUTORESET_ENTRY_SD, set_wd_autoreset_pfs,	/* write */
						get_wd_autoreset_pfs,	/* read  */
						procfs_dir, pbp_device_block))
			ret = -1;
		if (bypass_proc_create_entry_sd(&(current_pfs->tpl), TPL_ENTRY_SD, set_tpl_pfs,	/* write */
						get_tpl_pfs,	/* read  */
						procfs_dir, pbp_device_block))
			ret = -1;
#ifdef PMC_FIX_FLAG
		if (bypass_proc_create_entry_sd(&(current_pfs->tpl), WAIT_AT_PWUP_ENTRY_SD, set_wait_at_pwup_pfs,	/* write */
						get_wait_at_pwup_pfs,	/* read  */
						procfs_dir, pbp_device_block))
			ret = -1;
		if (bypass_proc_create_entry_sd(&(current_pfs->tpl), HW_RESET_ENTRY_SD, set_hw_reset_pfs,	/* write */
						get_hw_reset_pfs,	/* read  */
						procfs_dir, pbp_device_block))
			ret = -1;

#endif

	}
	if (ret < 0)
		printk(KERN_DEBUG "Create proc entry failed\n");

	return ret;
}

int bypass_proc_remove_dev_sd(bpctl_dev_t *pbp_device_block)
{

	struct bypass_pfs_sd *current_pfs = &pbp_device_block->bypass_pfs_set;
	struct proc_dir_entry *pde = current_pfs->bypass_entry, *pde_curr =
	    NULL;
	char name[256];

	if (!pde)
		return 0;
	for (pde = pde->subdir; pde;) {
		strcpy(name, pde->name);
		pde_curr = pde;
		pde = pde->next;
		remove_proc_entry(name, current_pfs->bypass_entry);
	}
	if (!pde)
		remove_proc_entry(current_pfs->dir_name, bp_procfs_dir);
	current_pfs->bypass_entry = NULL;

	return 0;
}

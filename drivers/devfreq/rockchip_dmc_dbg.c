// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020, Rockchip Electronics Co., Ltd.
 */
#include <linux/module.h>
#include <linux/nospec.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/rockchip/rockchip_sip.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include <soc/rockchip/rockchip_sip.h>

#include "rockchip_dmc_timing.h"

/*
 * DMCDBG share memory request 4KB for delivery parameter
 */
#define DMCDBG_PAGE_NUMS			(1)
#define DMCDBG_SHARE_MEM_SIZE			((DMCDBG_PAGE_NUMS) * 4096)

#define PROC_DMCDBG_DIR_NAME			"dmcdbg"
#define PROC_DMCDBG_DRAM_INFO			"dmcinfo"
#define PROC_DMCDBG_POWERSAVE			"powersave"
#define PROC_DMCDBG_DRVODT			"drvodt"
#define PROC_DMCDBG_DESKEW			"deskew"
#define PROC_DMCDBG_REGS_INFO			"regsinfo"

#define DDRDBG_FUNC_GET_VERSION			(0x01)
#define DDRDBG_FUNC_GET_SUPPORTED		(0x02)
#define DDRDBG_FUNC_GET_DRAM_INFO		(0x03)
#define DDRDBG_FUNC_GET_DESKEW_INFO		(0x04)
#define DDRDBG_FUNC_UPDATE_DESKEW		(0x05)
#define DDRDBG_FUNC_DATA_TRAINING		(0x06)
#define DDRDBG_FUNC_UPDATE_DESKEW_TR		(0x07)
#define DDRDBG_FUNC_GET_POWERSAVE_INFO		(0x08)
#define DDRDBG_FUNC_UPDATE_POWERSAVE		(0x09)
#define DDRDBG_FUNC_GET_DRVODT_INFO		(0x0a)
#define DDRDBG_FUNC_UPDATE_DRVODT		(0x0b)
#define DDRDBG_FUNC_GET_REGISTERS_INFO		(0x0c)

#define DRV_ODT_UNKNOWN				(0xffff)
#define DRV_ODT_UNSUSPEND_FIX			(0x0)
#define DRV_ODT_SUSPEND_FIX			(0x1)

#define REGS_NAME_LEN_MAX			(20)
#define SKEW_GROUP_NUM_MAX			(6)
#define SKEW_TIMING_NUM_MAX			(50)

struct rockchip_dmcdbg {
	struct device *dev;
};

struct proc_dir_entry *proc_dmcdbg_dir;

struct dram_cap_info {
	unsigned int rank;
	unsigned int col;
	unsigned int bank;
	unsigned int buswidth;
	unsigned int die_buswidth;
	unsigned int row_3_4;
	unsigned int cs0_row;
	unsigned int cs1_row;
	unsigned int cs0_high16bit_row;
	unsigned int cs1_high16bit_row;
	unsigned int bankgroup;
	unsigned int size;
};

struct dram_info {
	unsigned int version;
	char dramtype[10];
	unsigned int dramfreq;
	unsigned int channel_num;
	struct dram_cap_info ch[2];
};

static const char * const power_save_msg[] = {
	"auto power down enable",
	"auto power down idle cycle",
	"auto self refresh enable",
	"auto self refresh idle cycle",
	"self refresh with clock gate idle cycle",
	"self refresh and power down lite idle cycle",
	"standby idle cycle",
};

struct power_save_info {
	unsigned int pd_en;
	unsigned int pd_idle;
	unsigned int sr_en;
	unsigned int sr_idle;
	unsigned int sr_mc_gate_idle;
	unsigned int srpd_lite_idle;
	unsigned int standby_idle;
};

static const char * const drv_odt_msg[] = {
	"dram side drv pull-up",
	"dram side drv pull-down",
	"dram side dq odt pull-up",
	"dram side dq odt pull-down",
	"dram side ca odt pull-up",
	"dram side ca odt pull-down",
	"soc side ca drv pull-up",
	"soc side ca drv pull-down",
	"soc side ck drv pull-up",
	"soc side ck drv pull-down",
	"soc side cs drv pull-up",
	"soc side cs drv pull-down",
	"soc side dq drv pull-up",
	"soc side dq drv pull-down",
	"soc side odt pull-up",
	"soc side odt pull-down",
	"phy vref inner",
	"phy vref out",
};

struct drv_odt {
	unsigned int value;
	unsigned int ohm;
	unsigned int flag;
};

struct drv_odt_vref {
	unsigned int value;
	unsigned int percen;
	unsigned int flag;
};

struct drv_odt_info {
	struct drv_odt dram_drv_up;
	struct drv_odt dram_drv_down;
	struct drv_odt dram_dq_odt_up;
	struct drv_odt dram_dq_odt_down;
	struct drv_odt dram_ca_odt_up;
	struct drv_odt dram_ca_odt_down;
	struct drv_odt phy_ca_drv_up;
	struct drv_odt phy_ca_drv_down;
	struct drv_odt phy_ck_drv_up;
	struct drv_odt phy_ck_drv_down;
	struct drv_odt phy_cs_drv_up;
	struct drv_odt phy_cs_drv_down;
	struct drv_odt phy_dq_drv_up;
	struct drv_odt phy_dq_drv_down;
	struct drv_odt phy_odt_up;
	struct drv_odt phy_odt_down;
	struct drv_odt_vref phy_vref_inner;
	struct drv_odt_vref phy_vref_out;
};

struct dmc_registers {
	char regs_name[REGS_NAME_LEN_MAX];
	unsigned int regs_addr;
};

struct registers_info {
	unsigned int regs_num;
	struct dmc_registers regs[];
};

struct skew_group {
	unsigned int skew_num;
	unsigned int *p_skew_info;
	char *p_skew_timing[SKEW_TIMING_NUM_MAX];
	char *note;
};

struct rockchip_dmcdbg_data {
	unsigned int inited_flag;
	void __iomem *share_memory;
	unsigned int skew_group_num;
	struct skew_group skew_group[SKEW_GROUP_NUM_MAX];
};

static struct rockchip_dmcdbg_data dmcdbg_data;

struct skew_info_rv1126 {
	unsigned int ca_skew[32];
	unsigned int cs0_a_skew[44];
	unsigned int cs0_b_skew[44];
	unsigned int cs1_a_skew[44];
	unsigned int cs1_b_skew[44];
};

static int dmcinfo_proc_show(struct seq_file *m, void *v)
{
	struct arm_smccc_res res;
	struct dram_info *p_dram_info;
	struct file *fp  = NULL;
	char cur_freq[20] = {0};
	char governor[20] = {0};
	loff_t pos;
	u32 i;

	res = sip_smc_dram(SHARE_PAGE_TYPE_DDRDBG, DDRDBG_FUNC_GET_DRAM_INFO,
			   ROCKCHIP_SIP_CONFIG_DRAM_DEBUG);
	if (res.a0) {
		seq_printf(m, "rockchip_sip_config_dram_debug error:%lx\n",
			   res.a0);
		return -ENOMEM;
	}

	if (!dmcdbg_data.inited_flag) {
		seq_puts(m, "dmcdbg_data no int\n");
		return -EPERM;
	}
	p_dram_info = (struct dram_info *)dmcdbg_data.share_memory;

	/* dram type information */
	seq_printf(m,
		   "DramType:	%s\n"
		   ,
		   p_dram_info->dramtype
		   );

	/* dram capacity information */
	seq_printf(m,
		   "\n"
		   "DramCapacity:\n"
		   );

	for (i = 0; i < p_dram_info->channel_num; i++) {
		if (p_dram_info->channel_num == 2)
			seq_printf(m,
				   "Channel [%d]:\n"
				   ,
				   i
				   );

		seq_printf(m,
			   "CS Count:	%d\n"
			   "Bus Width:	%d bit\n"
			   "Column:		%d\n"
			   "Bank:		%d\n"
			   "CS0_Row:	%d\n"
			   "CS1_Row:	%d\n"
			   "DieBusWidth:	%d bit\n"
			   "TotalSize:	%d MB\n"
			   ,
			   p_dram_info->ch[i].rank,
			   p_dram_info->ch[i].buswidth,
			   p_dram_info->ch[i].col,
			   p_dram_info->ch[i].bank,
			   p_dram_info->ch[i].cs0_row,
			   p_dram_info->ch[i].cs1_row,
			   p_dram_info->ch[i].die_buswidth,
			   p_dram_info->ch[i].size
			   );
	}

	/* check devfreq/dmc device */
	fp = filp_open("/sys/class/devfreq/dmc/cur_freq", O_RDONLY, 0);
	if (IS_ERR(fp)) {
		seq_printf(m,
			   "\n"
			   "devfreq/dmc:	Disable\n"
			   "DramFreq:	%d\n"
			   ,
			   p_dram_info->dramfreq
			   );
	} else {
		pos = 0;
		kernel_read(fp, cur_freq, sizeof(cur_freq), &pos);
		filp_close(fp, NULL);

		fp = filp_open("/sys/class/devfreq/dmc/governor", O_RDONLY, 0);
		if (IS_ERR(fp)) {
			fp = NULL;
		} else {
			pos = 0;
			kernel_read(fp, governor, sizeof(governor), &pos);
			filp_close(fp, NULL);
		}

		seq_printf(m,
			   "\n"
			   "devfreq/dmc:	Enable\n"
			   "governor:	%s\n"
			   "cur_freq:	%s\n"
			   ,
			   governor,
			   cur_freq
			   );
		seq_printf(m,
			   "NOTE:\n"
			   "more information about dmc can get from /sys/class/devfreq/dmc.\n"
			   );
	}

	return 0;
}

static int dmcinfo_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, dmcinfo_proc_show, NULL);
}

static const struct file_operations dmcinfo_proc_fops = {
	.open		= dmcinfo_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int proc_dmcinfo_init(void)
{
	/* create dmcinfo file */
	proc_create(PROC_DMCDBG_DRAM_INFO, 0644, proc_dmcdbg_dir,
		    &dmcinfo_proc_fops);

	return 0;
}

static int powersave_proc_show(struct seq_file *m, void *v)
{
	struct arm_smccc_res res;
	struct power_save_info *p_power;
	unsigned int *p_uint;
	unsigned int i = 0;

	/* get low power information */
	res = sip_smc_dram(SHARE_PAGE_TYPE_DDRDBG,
			   DDRDBG_FUNC_GET_POWERSAVE_INFO,
			   ROCKCHIP_SIP_CONFIG_DRAM_DEBUG);
	if (res.a0) {
		seq_printf(m, "rockchip_sip_config_dram_debug error:%lx\n",
			   res.a0);
		return -ENOMEM;
	}

	if (!dmcdbg_data.inited_flag) {
		seq_puts(m, "dmcdbg_data no int\n");
		return -EPERM;
	}
	p_power = (struct power_save_info *)dmcdbg_data.share_memory;

	seq_printf(m,
		   "low power information:\n"
		   "\n"
		   "[number]name: value\n"
		   );

	p_uint = (unsigned int *)p_power;
	for (i = 0; i < ARRAY_SIZE(power_save_msg); i++)
		seq_printf(m,
			   "[%d]%s: %d\n"
			   ,
			   i, power_save_msg[i], *(p_uint + i)
			   );

	seq_printf(m,
		   "\n"
		   "power save setting:\n"
		   "echo number=value > /proc/dmcdbg/powersave\n"
		   "eg: set auto power down enable to 1\n"
		   "  echo 0=1 > /proc/dmcdbg/powersave\n"
		   "\n"
		   "Support for setting multiple parameters at the same time.\n"
		   "echo number=value,number=value,... > /proc/dmcdbg/powersave\n"
		   "eg:\n"
		   "  echo 0=1,1=32 > /proc/dmcdbg/powersave\n"
		   );

	return 0;
}

static int powersave_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, powersave_proc_show, NULL);
}

static ssize_t powersave_proc_write(struct file *file,
				     const char __user *buffer,
				     size_t count, loff_t *ppos)
{
	struct arm_smccc_res res;
	struct power_save_info *p_power;
	unsigned int *p_uint;
	char *buf, *cookie_pot, *p_char;
	int ret = 0;
	u32 loop, i, offset, value;
	long long_val;

	/* get buffer data */
	buf = vzalloc(count);
	cookie_pot = buf;
	if (!cookie_pot)
		return -ENOMEM;

	if (copy_from_user(cookie_pot, buffer, count)) {
		ret = -EFAULT;
		goto err;
	}

	/* get power save setting information */
	res = sip_smc_dram(SHARE_PAGE_TYPE_DDRDBG,
			   DDRDBG_FUNC_GET_POWERSAVE_INFO,
			   ROCKCHIP_SIP_CONFIG_DRAM_DEBUG);
	if (res.a0) {
		pr_err("rockchip_sip_config_dram_debug error:%lx\n", res.a0);
		ret = -ENOMEM;
		goto err;
	}

	if (!dmcdbg_data.inited_flag) {
		pr_err("dmcdbg_data no int\n");
		ret = -EPERM;
		goto err;
	}
	p_power = (struct power_save_info *)dmcdbg_data.share_memory;

	loop = 0;
	for (i = 0; i < count; i++) {
		if (*(cookie_pot + i) == '=')
			loop++;
	}

	p_uint = (unsigned int *)p_power;
	for (i = 0; i < loop; i++) {
		p_char = strsep(&cookie_pot, "=");
		ret = kstrtol(p_char, 10, &long_val);
		if (ret)
			goto err;
		offset = long_val;

		if (i == (loop - 1))
			p_char = strsep(&cookie_pot, "\0");
		else
			p_char = strsep(&cookie_pot, ",");

		ret = kstrtol(p_char, 10, &long_val);
		if (ret)
			goto err;
		value = long_val;

		if (offset >= ARRAY_SIZE(power_save_msg)) {
			ret = -EINVAL;
			goto err;
		}
		offset = array_index_nospec(offset, ARRAY_SIZE(power_save_msg));

		*(p_uint + offset) = value;
	}

	/* update power save setting */
	res = sip_smc_dram(SHARE_PAGE_TYPE_DDRDBG, DDRDBG_FUNC_UPDATE_POWERSAVE,
			   ROCKCHIP_SIP_CONFIG_DRAM_DEBUG);
	if (res.a0) {
		pr_err("rockchip_sip_config_dram_debug error:%lx\n", res.a0);
		ret = -ENOMEM;
		goto err;
	}

	ret = count;
err:
	vfree(buf);
	return ret;
}

static const struct file_operations powersave_proc_fops = {
	.open		= powersave_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= powersave_proc_write,
};

static int proc_powersave_init(void)
{
	/* create dmcinfo file */
	proc_create(PROC_DMCDBG_POWERSAVE, 0644, proc_dmcdbg_dir,
		    &powersave_proc_fops);

	return 0;
}

static int drvodt_proc_show(struct seq_file *m, void *v)
{
	struct arm_smccc_res res;
	struct drv_odt_info *p_drvodt;
	unsigned int *p_uint;
	unsigned int i;

	/* get drive strength and odt information */
	res = sip_smc_dram(SHARE_PAGE_TYPE_DDRDBG, DDRDBG_FUNC_GET_DRVODT_INFO,
			   ROCKCHIP_SIP_CONFIG_DRAM_DEBUG);
	if (res.a0) {
		seq_printf(m, "rockchip_sip_config_dram_debug error:%lx\n",
			   res.a0);
		return -ENOMEM;
	}

	if (!dmcdbg_data.inited_flag) {
		seq_puts(m, "dmcdbg_data no int\n");
		return -EPERM;
	}
	p_drvodt = (struct drv_odt_info *)dmcdbg_data.share_memory;

	seq_printf(m,
		   "drv and odt information:\n"
		   "\n"
		   "[number]name: value (ohm)\n"
	);

	p_uint = (unsigned int *)p_drvodt;
	for (i = 0; i < ARRAY_SIZE(drv_odt_msg); i++) {
		if (*(p_uint + (i * 3)) == DRV_ODT_UNKNOWN)
			seq_printf(m,
				   "[%2d]%s: NULL (unknown) %c\n"
				   ,
				   i, drv_odt_msg[i],
				   (*(p_uint + (i * 3) + 2) ==
				    DRV_ODT_SUSPEND_FIX) ? '\0' : '*'
			);
		else if (*(p_uint + (i * 3) + 1) == DRV_ODT_UNKNOWN)
			seq_printf(m,
				   "[%2d]%s: %d (unknown) %c\n"
				   ,
				   i, drv_odt_msg[i], *(p_uint + (i * 3)),
				   (*(p_uint + (i * 3) + 2) ==
				    DRV_ODT_SUSPEND_FIX) ? '\0' : '*'
			);
		else if (i < (ARRAY_SIZE(drv_odt_msg) - 2))
			seq_printf(m,
				   "[%2d]%s: %d (%d ohm) %c\n"
				   ,
				   i, drv_odt_msg[i], *(p_uint + (i * 3)),
				   *(p_uint + (i * 3) + 1),
				   (*(p_uint + (i * 3) + 2) ==
				    DRV_ODT_SUSPEND_FIX) ? '\0' : '*'
			);
		else
			seq_printf(m,
				   "[%2d]%s: %d (%d %%) %c\n"
				   ,
				   i, drv_odt_msg[i], *(p_uint + (i * 3)),
				   *(p_uint + (i * 3) + 1),
				   (*(p_uint + (i * 3) + 2) ==
				    DRV_ODT_SUSPEND_FIX) ? '\0' : '*'
			);
	}

	seq_printf(m,
		   "\n"
		   "drvodt setting:\n"
		   "echo number=value > /proc/dmcdbg/drvodt\n"
		   "eg: set soc side ca drv up to 20\n"
		   "  echo 6=20 > /proc/dmcdbg/drvodt\n"
		   "\n"
		   "Support for setting multiple parameters at the same time.\n"
		   "echo number=value,number=value,... > /proc/dmcdbg/drvodt\n"
		   "eg: set soc side ca drv up and down to 20\n"
		   "  echo 6=20,7=20 > /proc/dmcdbg/drvodt\n"
		   "Note: Please update both up and down at the same time.\n"
		   "      (*) mean unsupported setting value\n"
	);

	return 0;
}

static int drvodt_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, drvodt_proc_show, NULL);
}

static ssize_t drvodt_proc_write(struct file *file,
				 const char __user *buffer,
				 size_t count, loff_t *ppos)
{
	struct arm_smccc_res res;
	struct drv_odt_info *p_drvodt;
	unsigned int *p_uint;
	char *buf, *cookie_pot, *p_char;
	int ret = 0;
	u32 loop, i, offset, value;
	long long_val;

	/* get buffer data */
	buf = vzalloc(count);
	cookie_pot = buf;
	if (!cookie_pot)
		return -ENOMEM;

	if (copy_from_user(cookie_pot, buffer, count)) {
		ret = -EFAULT;
		goto err;
	}

	/* get drv and odt setting  */
	res = sip_smc_dram(SHARE_PAGE_TYPE_DDRDBG, DDRDBG_FUNC_GET_DRVODT_INFO,
			   ROCKCHIP_SIP_CONFIG_DRAM_DEBUG);
	if (res.a0) {
		pr_err("rockchip_sip_config_dram_debug error:%lx\n", res.a0);
		ret = -ENOMEM;
		goto err;
	}

	if (!dmcdbg_data.inited_flag) {
		pr_err("dmcdbg_data no int\n");
		ret = -EPERM;
		goto err;
	}
	p_drvodt = (struct drv_odt_info *)dmcdbg_data.share_memory;

	loop = 0;
	for (i = 0; i < count; i++) {
		if (*(cookie_pot + i) == '=')
			loop++;
	}

	p_uint = (unsigned int *)p_drvodt;
	for (i = 0; i < loop; i++) {
		p_char = strsep(&cookie_pot, "=");
		ret = kstrtol(p_char, 10, &long_val);
		if (ret)
			goto err;
		offset = long_val;

		if (i == (loop - 1))
			p_char = strsep(&cookie_pot, "\0");
		else
			p_char = strsep(&cookie_pot, ",");

		ret = kstrtol(p_char, 10, &long_val);
		if (ret)
			goto err;
		value = long_val;

		if (offset >= ARRAY_SIZE(drv_odt_msg)) {
			ret = -EINVAL;
			goto err;
		}
		offset *= 3;
		offset = array_index_nospec(offset, ARRAY_SIZE(drv_odt_msg) * 3);

		*(p_uint + offset) = value;
	}

	/* update power save setting */
	res = sip_smc_dram(SHARE_PAGE_TYPE_DDRDBG, DDRDBG_FUNC_UPDATE_DRVODT,
			   ROCKCHIP_SIP_CONFIG_DRAM_DEBUG);
	if (res.a0) {
		pr_err("rockchip_sip_config_dram_debug error:%lx\n", res.a0);
		ret = -ENOMEM;
		goto err;
	}

	ret = count;
err:
	vfree(buf);
	return ret;
}

static const struct file_operations drvodt_proc_fops = {
	.open		= drvodt_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= drvodt_proc_write,
};

static int proc_drvodt_init(void)
{
	/* create dmcinfo file */
	proc_create(PROC_DMCDBG_DRVODT, 0644, proc_dmcdbg_dir,
		    &drvodt_proc_fops);

	return 0;
}

static int skew_proc_show(struct seq_file *m, void *v)
{
	struct arm_smccc_res res;
	unsigned int *p_uint;
	u32 group, i;

	/* get deskew information */
	res = sip_smc_dram(SHARE_PAGE_TYPE_DDRDBG, DDRDBG_FUNC_GET_DESKEW_INFO,
			   ROCKCHIP_SIP_CONFIG_DRAM_DEBUG);
	if (res.a0) {
		seq_printf(m, "rockchip_sip_config_dram_debug error:%lx\n",
			   res.a0);
		return -ENOMEM;
	}

	if (!dmcdbg_data.inited_flag) {
		seq_puts(m, "dmcdbg_data no int\n");
		return -EPERM;
	}

	seq_printf(m,
		   "de-skew information:\n"
		   "\n"
		   "[group_number]name: value\n"
	);

	for (group = 0; group < dmcdbg_data.skew_group_num; group++) {
		if (dmcdbg_data.skew_group[group].note != NULL)
			seq_printf(m,
				"%s\n"
				,
				dmcdbg_data.skew_group[group].note
			);
		p_uint = (unsigned int *)dmcdbg_data.skew_group[group].p_skew_info;
		for (i = 0; i < dmcdbg_data.skew_group[group].skew_num; i++)
			seq_printf(m,
				"[%c%d_%d]%s: %d\n"
				,
				(i < 10) ? ' ' : '\0', group, i,
				dmcdbg_data.skew_group[group].p_skew_timing[i],
				*(p_uint + i)
			);
	}

	seq_printf(m,
		   "\n"
		   "de-skew setting:\n"
		   "echo group_number=value > /proc/dmcdbg/deskew\n"
		   "eg: set a1_ddr3a14_de-skew to 8\n"
		   "  echo 0_1=8 > /proc/dmcdbg/deskew\n"
		   "\n"
		   "Support for setting multiple parameters simultaneously.\n"
		   "echo group_number=value,group_number=value,... > /proc/dmcdbg/deskew\n"
		   "eg:\n"
		   "  echo 0_1=8,1_2=8 > /proc/dmcdbg/deskew\n"
	);

	return 0;
}

static int skew_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, skew_proc_show, NULL);
}

static ssize_t skew_proc_write(struct file *file,
			       const char __user *buffer,
			       size_t count, loff_t *ppos)
{
	struct arm_smccc_res res;
	unsigned int *p_uint;
	char *buf, *cookie_pot, *p_char;
	int ret = 0;
	u32 loop, i, offset_max, group, offset, value;
	long long_val;

	/* get buffer data */
	buf = vzalloc(count);
	cookie_pot = buf;
	if (!cookie_pot)
		return -ENOMEM;

	if (copy_from_user(cookie_pot, buffer, count)) {
		ret = -EFAULT;
		goto err;
	}

	/* get skew setting  */
	res = sip_smc_dram(SHARE_PAGE_TYPE_DDRDBG, DDRDBG_FUNC_GET_DESKEW_INFO,
			   ROCKCHIP_SIP_CONFIG_DRAM_DEBUG);
	if (res.a0) {
		pr_err("rockchip_sip_config_dram_debug error:%lx\n", res.a0);
		ret = -ENOMEM;
		goto err;
	}

	if (!dmcdbg_data.inited_flag) {
		pr_err("dmcdbg_data no int\n");
		ret = -EPERM;
		goto err;
	}

	loop = 0;
	for (i = 0; i < count; i++) {
		if (*(cookie_pot + i) == '=')
			loop++;
	}

	for (i = 0; i < loop; i++) {
		p_char = strsep(&cookie_pot, "_");
		ret = kstrtol(p_char, 10, &long_val);
		if (ret)
			goto err;
		group = long_val;

		p_char = strsep(&cookie_pot, "=");
		ret = kstrtol(p_char, 10, &long_val);
		if (ret)
			goto err;
		offset = long_val;

		if (i == (loop - 1))
			p_char = strsep(&cookie_pot, "\0");
		else
			p_char = strsep(&cookie_pot, ",");

		ret = kstrtol(p_char, 10, &long_val);
		if (ret)
			goto err;
		value = long_val;

		if (group >= dmcdbg_data.skew_group_num) {
			ret = -EINVAL;
			goto err;
		}
		group = array_index_nospec(group, dmcdbg_data.skew_group_num);

		p_uint = (unsigned int *)dmcdbg_data.skew_group[group].p_skew_info;
		offset_max = dmcdbg_data.skew_group[group].skew_num;

		if (offset >= offset_max) {
			ret = -EINVAL;
			goto err;
		}
		offset = array_index_nospec(offset, offset_max);

		*(p_uint + offset) = value;
	}

	/* update power save setting */
	res = sip_smc_dram(SHARE_PAGE_TYPE_DDRDBG, DDRDBG_FUNC_UPDATE_DESKEW,
			   ROCKCHIP_SIP_CONFIG_DRAM_DEBUG);
	if (res.a0) {
		pr_err("rockchip_sip_config_dram_debug error:%lx\n", res.a0);
		ret = -ENOMEM;
		goto err;
	}

	ret = count;
err:
	vfree(buf);
	return ret;
}

static const struct file_operations skew_proc_fops = {
	.open		= skew_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= skew_proc_write,
};

static int proc_skew_init(void)
{
	/* create dmcinfo file */
	proc_create(PROC_DMCDBG_DESKEW, 0644, proc_dmcdbg_dir,
		    &skew_proc_fops);

	return 0;
}

static int regsinfo_proc_show(struct seq_file *m, void *v)
{
	struct arm_smccc_res res;
	struct registers_info *p_regsinfo;
	u32 i;

	res = sip_smc_dram(SHARE_PAGE_TYPE_DDRDBG,
			   DDRDBG_FUNC_GET_REGISTERS_INFO,
			   ROCKCHIP_SIP_CONFIG_DRAM_DEBUG);
	if (res.a0) {
		seq_printf(m, "rockchip_sip_config_dram_debug error:%lx\n",
			   res.a0);
		return -ENOMEM;
	}

	if (!dmcdbg_data.inited_flag) {
		seq_puts(m, "dmcdbg_data no int\n");
		return -EPERM;
	}
	p_regsinfo = (struct registers_info *)dmcdbg_data.share_memory;

	seq_printf(m,
		   "registers base address information:\n"
		   "\n"
	);

	for (i = 0; i < p_regsinfo->regs_num; i++) {
		seq_printf(m,
			   "%s=0x%x\n"
			   ,
			   p_regsinfo->regs[i].regs_name,
			   p_regsinfo->regs[i].regs_addr
			   );
	}

	return 0;
}

static int regsinfo_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, regsinfo_proc_show, NULL);
}

static const struct file_operations regsinfo_proc_fops = {
	.open		= regsinfo_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int proc_regsinfo_init(void)
{
	/* create dmcinfo file */
	proc_create(PROC_DMCDBG_REGS_INFO, 0644, proc_dmcdbg_dir,
		    &regsinfo_proc_fops);

	return 0;
}

static void rv1126_get_skew_parameter(void)
{
	struct skew_info_rv1126 *p_skew;
	u32 i;

	/* get skew parameters */
	p_skew = (struct skew_info_rv1126 *)dmcdbg_data.share_memory;
	dmcdbg_data.skew_group_num = 5;

	/* ca_skew parameters */
	dmcdbg_data.skew_group[0].p_skew_info = (unsigned int *)p_skew->ca_skew;
	dmcdbg_data.skew_group[0].skew_num = ARRAY_SIZE(rv1126_dts_ca_timing);
	for (i = 0; i < dmcdbg_data.skew_group[0].skew_num; i++)
		dmcdbg_data.skew_group[0].p_skew_timing[i] =
			(char *)rv1126_dts_ca_timing[i];
	dmcdbg_data.skew_group[0].note =
		"(ca_skew: ddr4(pad_name)_ddr3_lpddr3_lpddr4_de-skew)";

	/* cs0_a_skew parameters */
	dmcdbg_data.skew_group[1].p_skew_info = (unsigned int *)p_skew->cs0_a_skew;
	dmcdbg_data.skew_group[1].skew_num = ARRAY_SIZE(rv1126_dts_cs0_a_timing);
	for (i = 0; i < dmcdbg_data.skew_group[1].skew_num; i++)
		dmcdbg_data.skew_group[1].p_skew_timing[i] =
			(char *)rv1126_dts_cs0_a_timing[i];
	dmcdbg_data.skew_group[1].note = "(cs0_a_skew)";

	/* cs0_b_skew parameters */
	dmcdbg_data.skew_group[2].p_skew_info = (unsigned int *)p_skew->cs0_b_skew;
	dmcdbg_data.skew_group[2].skew_num = ARRAY_SIZE(rv1126_dts_cs0_b_timing);
	for (i = 0; i < dmcdbg_data.skew_group[2].skew_num; i++)
		dmcdbg_data.skew_group[2].p_skew_timing[i] =
			(char *)rv1126_dts_cs0_b_timing[i];
	dmcdbg_data.skew_group[2].note = "(cs0_b_skew)";

	/* cs1_a_skew parameters */
	dmcdbg_data.skew_group[3].p_skew_info = (unsigned int *)p_skew->cs1_a_skew;
	dmcdbg_data.skew_group[3].skew_num = ARRAY_SIZE(rv1126_dts_cs1_a_timing);
	for (i = 0; i < dmcdbg_data.skew_group[3].skew_num; i++)
		dmcdbg_data.skew_group[3].p_skew_timing[i] =
			(char *)rv1126_dts_cs1_a_timing[i];
	dmcdbg_data.skew_group[3].note = "(cs1_a_skew)";

	/* cs1_b_skew parameters */
	dmcdbg_data.skew_group[4].p_skew_info = (unsigned int *)p_skew->cs1_b_skew;
	dmcdbg_data.skew_group[4].skew_num = ARRAY_SIZE(rv1126_dts_cs1_b_timing);
	for (i = 0; i < dmcdbg_data.skew_group[3].skew_num; i++)
		dmcdbg_data.skew_group[4].p_skew_timing[i] =
			(char *)rv1126_dts_cs1_b_timing[i];
	dmcdbg_data.skew_group[4].note = "(cs1_b_skew)";
}

static __maybe_unused int rv1126_dmcdbg_init(struct platform_device *pdev,
					     struct rockchip_dmcdbg *dmcdbg)
{
	struct arm_smccc_res res;

	/* check ddr_debug_func version */
	res = sip_smc_dram(0, DDRDBG_FUNC_GET_VERSION,
			   ROCKCHIP_SIP_CONFIG_DRAM_DEBUG);
	dev_notice(&pdev->dev, "current ATF ddr_debug_func version 0x%lx.\n",
		   res.a1);
	/*
	 * [15:8] major version, [7:0] minor version
	 * major version must match both kernel dmcdbg and ATF ddr_debug_func.
	 */
	if (res.a0 || res.a1 < 0x101 || ((res.a1 & 0xff00) != 0x100)) {
		dev_err(&pdev->dev,
			"version invalid,need update,the major version unmatch!\n");
		return -ENXIO;
	}

	/* request share memory for pass parameter */
	res = sip_smc_request_share_mem(DMCDBG_PAGE_NUMS,
					SHARE_PAGE_TYPE_DDRDBG);
	if (res.a0 != 0) {
		dev_err(&pdev->dev, "request share mem error\n");
		return -ENOMEM;
	}

	dmcdbg_data.share_memory = (void __iomem *)res.a1;
	dmcdbg_data.inited_flag = 1;

	rv1126_get_skew_parameter();

	/* create parent dir in /proc */
	proc_dmcdbg_dir = proc_mkdir(PROC_DMCDBG_DIR_NAME, NULL);
	if (!proc_dmcdbg_dir) {
		dev_err(&pdev->dev, "create proc dir error!");
		return -ENOENT;
	}

	proc_dmcinfo_init();
	proc_powersave_init();
	proc_drvodt_init();
	proc_skew_init();
	proc_regsinfo_init();
	return 0;
}

static const struct of_device_id rockchip_dmcdbg_of_match[] = {
	{ .compatible = "rockchip,rv1126-dmcdbg", .data = rv1126_dmcdbg_init},
	{ },
};
MODULE_DEVICE_TABLE(of, rockchip_dmcdbg_of_match);

static int rockchip_dmcdbg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_dmcdbg *data;
	const struct of_device_id *match;
	int (*init)(struct platform_device *pdev,
		    struct rockchip_dmcdbg *data);
	int ret = 0;

	data = devm_kzalloc(dev, sizeof(struct rockchip_dmcdbg), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;

	/* match soc chip init */
	match = of_match_node(rockchip_dmcdbg_of_match, pdev->dev.of_node);
	if (match) {
		init = match->data;
		if (init) {
			if (init(pdev, data))
				return -EINVAL;
		}
	}

	return ret;
}

static struct platform_driver rockchip_dmcdbg_driver = {
	.probe	= rockchip_dmcdbg_probe,
	.driver = {
		.name	= "rockchip,dmcdbg",
		.of_match_table = rockchip_dmcdbg_of_match,
	},
};
module_platform_driver(rockchip_dmcdbg_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("YouMin Chen <cym@rock-chips.com>");
MODULE_DESCRIPTION("rockchip dmc debug driver with devfreq framework");

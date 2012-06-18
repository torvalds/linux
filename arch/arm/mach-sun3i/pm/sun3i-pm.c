/*
 * arch/arm/mach-sun3i/pm/sun3i-pm.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#if CONFIG_PM
#include <linux/module.h>
#include <linux/suspend.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/major.h>
#include <linux/device.h>
#include <asm/uaccess.h>
#include <asm/delay.h>
#include <asm/io.h>
#include <linux/power/sun3i_pm.h>
#include <../../../../drivers/power/sun3i_power/regulator/machine.h>
#include <../../../../drivers/power/sun3i_power/regulator/consumer.h>

#define DBG_PM_DEV		1
#if  DBG_PM_DEV
#define DBG_PM_MSG(format,args...)   printk("[pm]"format,##args)
#else
#define DBG_PM_MSG(format,args...)   do {} while (0)
#endif

#define PM_USE_SYSFS	0
#define PM_USE_PROC	0
#define PM_USE_NETLINK	0
#define AW_PMU_MAJOR	267

static struct aw_pm_info standby_info;
static struct cdev  *pmu_cdev=NULL;
static struct device *pmu_device=NULL;
static dev_t  pmu_dev;
static struct class *pm_class;

extern unsigned int save_sp(void);
extern void restore_sp(unsigned int sp);

/*
#define BUS_CCLK_MASK		(0xfffff0ff)
#define BUS_CCLK_32K		(0<<9)
#define BUS_CCLK_24M		(1<<9)
*/
static int aw_pm_valid(suspend_state_t state)
{
	DBG_PM_MSG("valid\n");
	return standby_info.func_addr?1:0;
}

int aw_pm_begin(suspend_state_t state)
{
	DBG_PM_MSG("begin\n");
	/*COREPLL to 32K*/
	//ori_corepll = readl(SW_CCM_AHB_APB_CFG_REG);
	//writel((ori_corepll&BUS_CCLK_MASK)|BUS_CCLK_32K,SW_CCM_AHB_APB_CFG_REG);
	//regulator_suspend_prepare(state);
	return 0;
}

int aw_pm_prepare(void)
{
	DBG_PM_MSG("prepare\n");
	return 0;
}

static int aw_pm_enter(suspend_state_t state)
{
	unsigned int tmp = 0;

	DBG_PM_MSG("enter standby, press any key to contine\n");

	/* copy standby.bin into sram */
	standby_info.sram_func = (void (*)(struct aw_pm_arg *))SRAM_FUNC_START;
	memcpy(standby_info.sram_func,standby_info.func_addr,standby_info.func_size);

	/* goto sram and run */
	tmp = save_sp();
	standby_info.sram_func(&standby_info.arg);
	restore_sp(tmp);
	//DBG_PM_MSG("old sp 0x%8x\n",tmp);

	return 0;
}

void aw_pm_finish(void)
{
	DBG_PM_MSG("finish\n");
}

void aw_pm_end(void)
{
	//static struct regulator *arm_regulator;

	DBG_PM_MSG("end\n");
	//writel(ori_corepll,SW_CCM_AHB_APB_CFG_REG);
	//arm_regulator = regulator_get(NULL, "vddcore");
	//regulator_set_voltage(arm_regulator, 1250000,1250000);
}

void aw_pm_recover(void)
{
	DBG_PM_MSG("recover\n");
}

static int aw_set_pmu(struct aw_pm_info *arg)
{
	void *buf = NULL;

	copy_from_user(&standby_info.arg,&arg->arg,sizeof(struct aw_pm_arg));
	buf = (char *)kmalloc(arg->func_size,GFP_KERNEL);
	if(!buf){
		printk(KERN_ERR"pmu malloc fail\n");
		return -ENOMEM;
	}
	copy_from_user(buf,arg->func_addr,arg->func_size);
	standby_info.func_size = arg->func_size;
	standby_info.func_addr = buf;

#if 0
	{
		int i;
		DBG_PM_MSG("func size=%d\n",standby_info.func_size);
		DBG_PM_MSG("mode=%x\n",standby_info.arg.wakeup_mode);
		for(i=0;i<4;i++)
			DBG_PM_MSG("%8x\t",standby_info.arg.param[i]);
		DBG_PM_MSG("\n");
	}
#endif
	return 0;
}

#if 0

/**************************
param definition
-For GPIO_MODE
param[0]: alarm ymd
param[1]: alarm hms
param[2]: output voltage
-For GEN_MODE
param[0]: power off alarm ymd
param[1]: power off alarm hms
param[2]: output voltage
param[3]: power on alarm ymd
param[4]: power on alarm hms
-For SPEC_MODE
param[0]: power off alarm ymd
param[1]: power off alarm hms
param[2]: pulse width
param[3]: power on alarm ymd
param[4]: power on alarm hms
***************************/
static int aw_set_alarm(unsigned int cmd,struct am_pm_arg *u_arg)
{
#if CONFIG_AM_CHIP_ID == 1211
	unsigned int reg_tmp,tmp;
	struct am_pm_arg arg;

	DBG_PM_MSG("[pm]set alarm\n");
	copy_from_user(&arg,u_arg,sizeof(struct am_pm_arg));

	reg_tmp = RTC_READ(RTC_ALARM);
	RTC_WRITE(reg_tmp&~(RTC_ALARM_EN|RTC_ALARM1_EN),RTC_ALARM);
	reg_tmp &= RTC_ALARM_CONF_MASK;
	switch(arg.wakeup_mode){
	case PM_GPIO_MODE:
		tmp = arg.param[2]&RTC_ALARM_GPO_MASK;
		reg_tmp |= RTC_ALARM_GPIO|RTC_ALARM_GPOE|tmp;
		break;
	case PM_SPEC_MODE:
		if((arg.param[2]>=RTC_ALARM_PW_MIN) && (arg.param[2]<=RTC_ALARM_PW_MAX))
			tmp = (arg.param[2])<<RTC_ALARM_PW_OFFSET;
		else
			return -EINVAL;
		reg_tmp |= RTC_ALARM_SPEC|RTC_ALARM_SMT_EN|tmp;
		break;
	case PM_GEN_MODE:
		if(!(reg_tmp&RTC_ALARM_GPOSEL_MASK)){
			tmp = arg.param[2]&RTC_ALARM_GPO_MASK;
		}else{
			tmp = (~(arg.param[2]))&RTC_ALARM_GPO_MASK;
		}
		reg_tmp |= RTC_ALARM_GEN|RTC_ALARM_GPOE|tmp;
		break;
	default:
		return -EINVAL;
	}

	if(arg.wakeup_mode != PM_GPIO_MODE){
		RTC_WRITE(arg.param[0],RTC_YMDALM);
		RTC_WRITE(arg.param[1],RTC_DHMSALM);
		RTC_WRITE(arg.param[3],RTC_YMDALM1);
		RTC_WRITE(arg.param[4],RTC_DHMSALM1);
		RTC_WRITE(reg_tmp|RTC_ALARM_EN|RTC_ALARM1_EN,RTC_ALARM);
	}else{
		RTC_WRITE(reg_tmp,RTC_ALARM);
	}
	//RTC_WRITE(RTC_READ(RTC_CTL)|RTC_ALARM_IRQ_EN,RTC_CTL);
#endif
	return 0;
}
#endif

static long aw_pmu_ioctl(struct file *pfile, unsigned int cmd, unsigned long arg)
{
	int ret = -EINVAL;

	DBG_PM_MSG("ioctl\n");

	switch(cmd){
	case AW_PMU_SET:
		ret = aw_set_pmu((struct aw_pm_info *)arg);
		break;
	case AW_PMU_VALID:
		ret = aw_pm_valid(0);
		copy_to_user((void *)arg,&ret,sizeof(int));
		ret = 0;
		break;
#if 0
	case AM_PMU_POFF:
		ret = am_set_alarm(cmd,(struct am_pm_arg *)arg);
		break;
/*----------------------
	case AM_PMU_PLOW:
		am_change_ddr_clk(DDR_LOW_CLOCK);
		break;
	case AM_PMU_PHIGH:
		am_change_ddr_clk(DDR_HIGH_CLOCK);
		break;
--------------------------*/
	case AM_PMU_PLOW:
	case AM_PMU_PHIGH:
		{
			struct am_chpll_arg xarg;
			void (*change_ddr_clk)(int, int);
			unsigned long irq_flag;
			unsigned int busclk;
			int memsize;
			void *buf;
			void serial_setbrg(unsigned long);


			copy_from_user(&xarg, arg, sizeof(xarg));
			change_ddr_clk = xarg.sram_entry;

			printk(" Entry: 0x%08x\n", xarg.sram_entry);
			printk(" Code start: 0x%08x\n", xarg.code_start);
			printk(" Code size:  0x%08x\n", xarg.code_size);
			printk(" Clock:      0x%08x\n", xarg.clock);

			memsize = (xarg.code_size < 512 ? 512 : xarg.code_size);
			buf = kmalloc(memsize, GFP_KERNEL);
			copy_from_user(buf, xarg.code_start, xarg.code_size);
			mem_dump_long(buf, 64);
			printk("1. crc = 0x%08x\n", crc32(buf, xarg.code_size));

			local_irq_save(irq_flag);
			cache_exit();
			memcpy(xarg.sram_entry, buf, xarg.code_size);
			mem_dump_long(xarg.sram_entry, 64);
			printk("2. crc = 0x%08x\n", crc32(xarg.sram_entry, xarg.code_size));
			void enable_jtag(void);
			enable_jtag();
//			__asm__ __volatile__("1:	b 1b\n\t");
			busclk = act_readl(CMU_BUSCLK);
			act_writel(busclk | 4, CMU_BUSCLK);
			change_ddr_clk(xarg.clock, buf);
			act_writel(busclk, CMU_BUSCLK);
			serial_setbrg(115200);
			printk("=========HAHA===============\n");
			cache_init();
			kfree(buf);
			local_irq_restore(irq_flag);
		}
		break;
#endif
	default:
		break;
	}

	return ret;
}

static struct platform_suspend_ops aw_pm_ops = {
	.valid = aw_pm_valid,
	.begin = aw_pm_begin,
	.prepare = aw_pm_prepare,
	.enter = aw_pm_enter,
	.finish = aw_pm_finish,
	.end = aw_pm_end,
	.recover = aw_pm_recover,
};

static struct file_operations pmudev_fops= {
	.owner  = THIS_MODULE,
	.unlocked_ioctl = aw_pmu_ioctl,
};

#if 0
#include <linux/timer.h>
static struct timer_list pm_timer;
static void am_pm_timer_isr(unsigned long nr)
{
	//printk("pm timer\n");
	udelay(100);
	pm_timer.expires = jiffies + HZ/125;
	add_timer(&pm_timer);
}
#endif

#if PM_USE_SYSFS
static ssize_t aw_pm_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	char *s = buf;

	s += sprintf(s,"low\n");

	return (s-buf);
}

ssize_t aw_pm_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	DBG_PM_MSG("aw_pm_store\n");
	return count;
}

static DEVICE_ATTR(low,S_IRUGO | S_IWUSR,aw_pm_show,aw_pm_store);
#endif

#if PM_USE_PROC
#include <linux/proc_fs.h>
static int aw_pm_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	DBG_PM_MSG("aw_pm_read_proc\n");
	return 0;
}

static int aw_pm_write_proc(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
	DBG_PM_MSG("aw_pm_write_proc\n");
	return count;
}
#endif

#if PM_USE_NETLINK
#include <net/netlink.h>
#include <net/sock.h>
#include <linux/netlink.h>

#define PM_NETLINK_TEST 			31
#define NETLINK_TEST_K_MSG		32
static struct sock  *nl_sock = NULL;

static DEFINE_MUTEX(nl_mutex);

static int send_to_user(__u32 rec_pid)
{
	int size,ret;
	unsigned char *old_tail;
	__u32 *kdata;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;

	size = NLMSG_SPACE(sizeof(rec_pid));
	skb = alloc_skb(size,GFP_ATOMIC);
	if(!skb){
		printk("alloc skb failed\n");
		return -1;
	}
	old_tail = skb->tail;
	nlh = NLMSG_PUT(skb,0,0,NETLINK_TEST_K_MSG,size - sizeof(*nlh));
	kdata = NLMSG_DATA(nlh);
	*kdata = rec_pid;
	DBG_PM_MSG("nl_send:%d\n",*kdata);
	nlh->nlmsg_len = skb->tail - old_tail;
	NETLINK_CB(skb).pid = 0;
	NETLINK_CB(skb).dst_group = 0;

	ret = netlink_unicast(nl_sock,skb,rec_pid,MSG_DONTWAIT);

nlmsg_failure:
	return ret;
}

static void pm_nl_rec(struct sk_buff * skb)
{
	__u32 udata[4];
	struct nlmsghdr *nlh = NULL;

	DBG_PM_MSG("nl_rec:\n");
    	if(skb->len >= nlmsg_total_size(0)){ //sizeof(struct nlmsghdr)
        	nlh = nlmsg_hdr(skb);
        	if( (nlh->nlmsg_len >= sizeof(struct nlmsghdr))&& (skb->len >= nlh->nlmsg_len)){
                		printk("pid = %d,type=0x%x\n",nlh->nlmsg_pid,nlh->nlmsg_type);
				memcpy(udata,NLMSG_DATA(nlh),nlh->nlmsg_len-NLMSG_HDRLEN);
				printk("rec first data 0x%x, len is %d\n",udata[0],nlh->nlmsg_len-NLMSG_HDRLEN);
				send_to_user(nlh->nlmsg_pid);
         	}
	}
}
#endif

static int __init aw_pm_init(void)
{
	int result;
#if PM_USE_PROC
	struct proc_dir_entry *dir_entry=NULL;
#endif

	DBG_PM_MSG("init\n");

	pmu_dev =MKDEV(AW_PMU_MAJOR,0);
	result = register_chrdev_region(pmu_dev,PMU_MAX_DEVS,"aw_pmu");
	if(result){
		printk(KERN_ERR "alloc_chrdev_region() failed for pmu\n");
		return -EIO;
	}

	pmu_cdev = kzalloc(sizeof(struct cdev),GFP_KERNEL);
	if(!pmu_cdev){
		printk(KERN_ERR "malloc memory  fails for pmu device\n");
		unregister_chrdev_region(pmu_dev,PMU_MAX_DEVS);
		return -ENOMEM;
	}
  	cdev_init(pmu_cdev, &pmudev_fops);
	if(cdev_add(pmu_cdev, pmu_dev, 1))
		goto out_err;

	pm_class = class_create(THIS_MODULE, "pm_class");
    	if (IS_ERR(pm_class)){
			printk(KERN_ERR"create class error\n");
			return -EPERM;
	}

	pmu_device = device_create(pm_class, NULL, pmu_dev, NULL, "pm");
#if PM_USE_SYSFS
	sysfs_create_file(&(pmu_device->kobj),&dev_attr_low.attr);
#endif

#if PM_USE_PROC
	dir_entry = create_proc_entry("pm",0644,NULL);
	if(dir_entry){
		dir_entry->read_proc = aw_pm_read_proc;
		dir_entry->write_proc = aw_pm_write_proc;
	}
#endif

#if PM_USE_NETLINK
	nl_sock = netlink_kernel_create(&init_net, PM_NETLINK_TEST, 0, pm_nl_rec, &nl_mutex, THIS_MODULE);
	if(!nl_sock){
		printk(KERN_ERR"netlink create failed\n");
		return -EPERM;
	}
	//nf_register_hook(&netlink_test_ops);
#endif

	memset(&standby_info,0,sizeof(struct aw_pm_info));
	suspend_set_ops(&aw_pm_ops);

	return 0;

out_err:
	printk(KERN_ERR "register failed  for pmu device\n");
	kfree(pmu_cdev);
	unregister_chrdev_region(pmu_dev,PMU_MAX_DEVS);
	return -ENODEV;
}

static void __exit aw_pm_exit(void)
{
#if PM_USE_SYSFS
	sysfs_remove_file(&(pmu_device->kobj), &dev_attr_low.attr);
#endif

#if PM_USE_PROC
	remove_proc_entry("pm",NULL);
#endif

#if PM_USE_NETLINK
	if(nl_sock){
        	sock_release(nl_sock->sk_socket);
    	}
#endif
	device_destroy(pm_class,pmu_dev);
	class_destroy(pm_class);

	if(pmu_cdev)
	{
		cdev_del(pmu_cdev);
		kfree(pmu_cdev);
	}
	unregister_chrdev_region(pmu_dev,PMU_MAX_DEVS);
}

module_init(aw_pm_init);
module_exit(aw_pm_exit);
#endif


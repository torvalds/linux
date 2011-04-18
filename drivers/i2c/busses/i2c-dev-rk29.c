#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/jiffies.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <linux/rwsem.h>


#include <asm/uaccess.h>

#include "i2c-dev-rk29.h"
#include "../i2c-core.h"


#define I2C_DEV_SCL_RATE	100 * 1000

struct completion		i2c_dev_complete = {
	.done = -1,
};
struct i2c_dump_info	g_dump;

static void i2c_dev_get_list(struct i2c_list_info *list)
{
	struct i2c_devinfo	*devinfo;
	struct i2c_adapter *adap = NULL;
	int index;

	memset(list, 0, sizeof(struct i2c_list_info));
	
	down_read(&__i2c_board_lock);
	list_for_each_entry(devinfo, &__i2c_board_list, list) {
		if(devinfo->busnum >= MAX_I2C_BUS) {
			list->adap_nr = -1;
			up_read(&__i2c_board_lock);
			return;
		}
		adap = i2c_get_adapter(devinfo->busnum);
		if(adap != NULL) {
			list->adap[devinfo->busnum].id = adap->nr;
			strcpy(list->adap[devinfo->busnum].name, adap->name);

			index = list->adap[devinfo->busnum].client_nr++;
			if(index >= MAX_CLIENT_NUM || index == -1)
				list->adap[devinfo->busnum].client_nr = -1;
			else {
				list->adap[devinfo->busnum].client[index].addr = devinfo->board_info.addr;
				strcpy(list->adap[devinfo->busnum].client[index].name,
						devinfo->board_info.type);
			}
		}
	}
	list->adap_nr = MAX_I2C_BUS;
	up_read(&__i2c_board_lock);
	return;
}
void i2c_dev_dump_start(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	int i, j;
	
	memset(&g_dump, 0, sizeof(struct i2c_dump_info));
	g_dump.id = adap->nr;
	g_dump.addr = msgs[0].addr;
	
	for(i = 0; i < num; i++) {
		if(msgs[i].flags & I2C_M_RD) {
			if(msgs[i].len >= MAX_VALUE_NUM)
				g_dump.get_num = -1;
			else
				g_dump.get_num = msgs[i].len;
		}
		else {
			if(msgs[i].len >= MAX_VALUE_NUM)
				g_dump.set_num = -1;
			else {
				g_dump.set_num = msgs[i].len;
				for(j = 0; j < msgs[i].len; j++)
					g_dump.set_value[j] = msgs[i].buf[j];
			}
		}
	}
	return;
}
EXPORT_SYMBOL(i2c_dev_dump_start);

void i2c_dev_dump_stop(struct i2c_adapter *adap, struct i2c_msg *msgs, int num, int ret)
{
	int i, j;
	
	if(ret < 0) {
		g_dump.get_num = 0;
		g_dump.set_num = 0;
	}
	for(i = 0; i < num; i++) {
		if((msgs[i].flags & I2C_M_RD) && (g_dump.get_num > 0)) {
			for(j = 0; j < msgs[i].len; j++)
				g_dump.get_value[j] = msgs[i].buf[j];
		}
	}
	if(i2c_dev_complete.done == 0)
		complete(&i2c_dev_complete);
	return;
}
EXPORT_SYMBOL(i2c_dev_dump_stop);

static void i2c_dev_get_dump(struct i2c_dump_info *dump)
{
	init_completion(&i2c_dev_complete);
	wait_for_completion_killable(&i2c_dev_complete);
	*dump = g_dump;
	return;
}
static int i2c_dev_get_normal(struct i2c_adapter *adap, struct i2c_get_info *get)
{
	struct i2c_msg msg;
	char buf[MAX_VALUE_NUM];
	int ret, i;

	msg.addr = (__u16)get->addr;
	msg.flags = I2C_M_RD;
	msg.len = get->num;
	msg.buf = buf;
	msg.scl_rate = I2C_DEV_SCL_RATE;

	ret = i2c_transfer(adap, &msg, 1);
	if(ret == 1) {
		for(i = 0; i < get->num; i++)
			get->value[i] = buf[i];
		return 0;
	}
	else
		return -1;
}
static int i2c_dev_set_normal(struct i2c_adapter *adap, struct i2c_set_info *set)
{
	struct i2c_msg msg;
	char buf[MAX_VALUE_NUM];
	int ret;

	msg.addr = (__u16)set->addr;
	msg.flags = 0;
	msg.len = set->num;
	msg.buf = buf;
	msg.scl_rate = I2C_DEV_SCL_RATE;

	ret = i2c_transfer(adap, &msg, 1);
	return(ret == 1)? 0: -1;
}
static int i2c_dev_get_reg8(struct i2c_adapter *adap, struct i2c_get_info *get)
{
	int ret, i;
	struct i2c_msg msgs[2];
	char reg = get->reg;
	char buf[MAX_VALUE_NUM];
	
	msgs[0].addr = (__u16)get->addr;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &reg;
	msgs[0].scl_rate = I2C_DEV_SCL_RATE;

	msgs[1].addr = get->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = get->num;
	msgs[1].buf = buf;
	msgs[1].scl_rate = I2C_DEV_SCL_RATE;

	ret = i2c_transfer(adap, msgs, 2);
	if(ret == 2) {
		for(i = 0; i < get->num; i++)
			get->value[i] = buf[i];
		return 0;
	}
	else
		return -1;

}

static int i2c_dev_set_reg8(struct i2c_adapter *adap, struct i2c_set_info *set)
{
	int ret, i;
	struct i2c_msg msg;
	char buf[MAX_VALUE_NUM + 1];

	buf[0] = (char)set->reg;
	for(i = 0; i < set->num; i++) 
		buf[i+1] = (char)set->value[i];
	
	msg.addr = (__u16)set->addr;
	msg.flags = 0;
	msg.len = set->num + 1;
	msg.buf = buf;
	msg.scl_rate = I2C_DEV_SCL_RATE;


	ret = i2c_transfer(adap, &msg, 1);
	return (ret == 1)? 0: -1;
}

static int i2c_dev_get_reg16(struct i2c_adapter *adap, struct i2c_get_info *get)
{
	int ret, i;
	struct i2c_msg msgs[2];
	char reg[2];
	char buf[MAX_VALUE_NUM * 2];

	reg[0] = (char)(get->reg & 0xff);
	reg[1] = (char)((get->reg >>8) & 0xff);
	
	msgs[0].addr = (__u16)get->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = reg;
	msgs[0].scl_rate = I2C_DEV_SCL_RATE;

	msgs[1].addr = get->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = get->num * 2;
	msgs[1].buf = buf;
	msgs[1].scl_rate = I2C_DEV_SCL_RATE;

	ret = i2c_transfer(adap, msgs, 2);
	if(ret == 2) {
		for(i = 0; i < get->num; i++)
			get->value[i] = buf[2*i] & (buf[2*i+1]<<8);
		return 0;
	}
	else
		return -1;

}
static int i2c_dev_set_reg16(struct i2c_adapter *adap, struct i2c_set_info *set)
{
	struct i2c_msg msg;
	int ret, i;
	char buf[2 * (MAX_VALUE_NUM + 1)];

	buf[0] = (char)(set->reg & 0xff);
	buf[1] = (char)((set->reg >>8) & 0xff);
	
	for(i = 0; i < set->num; i++) {
		buf[2 * (i + 1)] = (char)(set->value[i] & 0xff);
		buf[2 * (i + 1) + 1] = (char)((set->value[i]>>8) & 0xff);
	}

	msg.addr = set->addr;
	msg.flags = 0;
	msg.len = 2 * (set->num + 1);
	msg.buf = buf;
	msg.scl_rate = I2C_DEV_SCL_RATE;

	ret = i2c_transfer(adap, &msg, 1);
	return (ret == 1)? 0: -1;
}

static int i2c_dev_get_value(struct i2c_get_info *get)
{
	int ret = 0;
	struct i2c_adapter *adap = NULL;
	
	if(get->num > MAX_VALUE_NUM)
		return -1;
	adap = i2c_get_adapter(get->id);
	if(adap == NULL)
		return -1;
	switch(get->mode) {
		case 'b': 
			ret = i2c_dev_get_reg8(adap, get);
			break;
		case 's':
			ret = i2c_dev_get_reg16(adap, get);
			break;
		case 'o':
			ret = -1;
			break;
		default:
			ret = i2c_dev_get_normal(adap, get);
			break;
	}
	return ret;
}

static int i2c_dev_set_value(struct i2c_set_info *set)
{
	int ret = 0;
	struct i2c_adapter *adap = NULL;

	printk("id=%d, addr=0x%x, mode = %c, num = %d, reg = 0x%x, value[0] = %d,",set->id, set->addr, set->mode, set->num, set->reg, set->value[0]);
	if(set->num > MAX_VALUE_NUM)
		return -1;
	adap = i2c_get_adapter(set->id);
	if(adap == NULL)
		return -1;
	switch(set->mode) {
		case 'b': 
			ret = i2c_dev_set_reg8(adap, set);
			break;
		case 's':
			ret = i2c_dev_set_reg16(adap, set);
			break;
		case 'o':
			ret = -1;
			break;
		default:
			ret = i2c_dev_set_normal(adap, set);
			break;
	}
	return ret;	
}

static int i2c_dev_open(struct inode *inode, struct file *file)
{
	return 0;
}
static long i2c_dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct i2c_list_info *list = NULL;
	struct i2c_dump_info dump;
	struct i2c_get_info get;
	struct i2c_set_info set;

	switch(cmd) {
		case I2C_LIST:
			list = kzalloc(sizeof(struct i2c_list_info), GFP_KERNEL);
			if(list == NULL) {
				ret = -ENOMEM;
				break;
			}
			i2c_dev_get_list(list);
			if(copy_to_user((void __user *)arg, (void *)list, sizeof(struct i2c_list_info)))  
				ret = -EFAULT;
			kfree(list);
			break;
		case I2C_DUMP:
			i2c_dev_get_dump(&dump);
			if(copy_to_user((void __user *)arg, (void *)&dump, sizeof(struct i2c_dump_info)))  
				ret = -EFAULT;
			break;
		case I2C_GET:
			if(copy_from_user((void *)&get, (void __user *)arg, sizeof(struct i2c_get_info))) { 
				ret = -EFAULT;
				break;
			}
			if(i2c_dev_get_value(&get) < 0) {
				ret = -EFAULT;
				break;
			}
			if(copy_to_user((void __user *)arg, (void *)&get, sizeof(struct i2c_get_info)))  
				ret = -EFAULT;
			break;
		case I2C_SET:
			if(copy_from_user((void *)&set, (void __user *)arg, sizeof(struct i2c_set_info))) { 
				ret = -EFAULT;
				break;
			}
			ret = i2c_dev_set_value(&set);
			break;
		default:
			break;
	}
	return ret;
}
static int i2c_dev_release(struct inode *inode, struct file *file)
{
	return 0;
}

static struct file_operations i2c_dev_fops = {
	.owner	 		 = THIS_MODULE,
	.open			 = i2c_dev_open,
	.unlocked_ioctl	 = i2c_dev_ioctl,
	.release 		 = i2c_dev_release,
};
static struct miscdevice i2c_misc_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = I2C_DEV_NAME,
	.fops = &i2c_dev_fops,
};
static int __init i2c_dev_init(void)
{
	return misc_register(&i2c_misc_dev);
}
static void __exit i2c_dev_exit(void)
{
	misc_deregister(&i2c_misc_dev);
}
module_init(i2c_dev_init);
module_exit(i2c_dev_exit);

MODULE_DESCRIPTION("Driver for RK29 I2C Device");
MODULE_AUTHOR("kfx, kfx@rock-chips.com");
MODULE_LICENSE("GPL");


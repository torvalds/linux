/*
 * drivers/amlogic/input/touchscreen/common.c
 *
 * 
 *	
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include "linux/amlogic/input/common.h"

static struct touch_pdata common = {
	.owner = NULL,
};
struct touch_pdata *ts_com = &common;
static struct file  *fp;
/***********************************************************************************************
Name	:	touch_open_fw
Input	:	*fw

Output	:	file size
function	:	open the firware file, and return total size
***********************************************************************************************/
int touch_open_fw(char *fw)
{
	loff_t file_size;
	mm_segment_t fs;
	struct inode *inode = NULL;
	fp = filp_open(fw, O_RDONLY, 0);
    if (IS_ERR(fp)) {
			//printk("read fw file error\n");
		return -1;
	}

	inode = fp->f_dentry->d_inode;
	file_size = inode->i_size;


	fs = get_fs();
	set_fs(KERNEL_DS);

	return 	file_size;

}

/***********************************************************************************************
Name	:	touch_read_fw
Input	:	offset
            length, read length
            buf, return buffer
Output	:
function	:	read data to buffer
***********************************************************************************************/
int touch_read_fw(int offset, int length, char *buf)
{
	loff_t  pos = offset;
	vfs_read(fp, buf, length, &pos);
	return 0;
}

int get_data_from_text_file(char *text_file, fill_buf_t fill_buf, void *priv)
{
	struct file  *fp;
	struct inode *inode = NULL;
	loff_t file_size, offset = 0;
	mm_segment_t fs;

	char text_buf[512];
	int text_len;
#define NO_COMMENT 0
#define LINE_COMMENT 1
#define BLOCK_COMMENT 2
	int comment = NO_COMMENT;
	char cur_char, last_char = 0;
	int i;
	
	char scan_buf[64];
	int scan_len;
	int idx=0;
	int ival=0;
	
	fp = filp_open(text_file, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		return -1;
	}
	inode = fp->f_dentry->d_inode;
	file_size = inode->i_size;
	fs = get_fs();
	set_fs(KERNEL_DS);
	
	scan_len = 0;
	memset(scan_buf, 0 ,ARRAY_SIZE(scan_buf));
	printk("open %s success, size=%d\n", text_file, (int)file_size);
	while (offset < file_size) {
		text_len = min_t(int, file_size-offset, ARRAY_SIZE(text_buf));
		vfs_read(fp, text_buf, text_len, &offset);		
		for(i=0; i<text_len; i++) {
			cur_char = text_buf[i];
			/* comment judge*/
			if (comment != NO_COMMENT) {
				if ((LINE_COMMENT == comment) && (0x0d == last_char) && (0x0a == cur_char)) {
					comment = NO_COMMENT;
				}
				else if ((BLOCK_COMMENT == comment) && ('*' == last_char) && ('/' == cur_char)) {
					comment = NO_COMMENT;
				}
			}
			else if ('/' == last_char) {
				if ('/' == cur_char) comment = LINE_COMMENT;
				else if ('*' == cur_char) comment = BLOCK_COMMENT;
			}
			else if ((cur_char <= ' ') || (',' == cur_char)
					  || ('{' == cur_char) || ('}' == cur_char)) {
				if (scan_len &&
				((sscanf(scan_buf,"0x%x",&ival)==1) || (sscanf(scan_buf,"%d",&ival)==1))) {
					fill_buf(priv, idx++, ival);
					//printk("%5d: val=0x8x,  string=%s\n", idx, ival, scan_buf);
				}
				scan_len = 0;
				memset(scan_buf, 0 ,ARRAY_SIZE(scan_buf));
			}
			else if (scan_len < ARRAY_SIZE(scan_buf)) {
				scan_buf[scan_len++] = cur_char;
			}
			last_char = cur_char;
		}
	}
	filp_close(fp, NULL);
	return idx;
}

/***********************************************************************************************
Name	:	touch_close_fw
Input	:
Output	:
function	:	close file
***********************************************************************************************/
int touch_close_fw(void)
{
	filp_close(fp, NULL);
	return 0;
}

ssize_t touch_read(struct device *dev, struct device_attribute *attr, char *buf)
{

    if (!strcmp(attr->attr.name, "PrintkFlag")) {
        memcpy(buf, &ts_com->printk_enable_flag,sizeof(ts_com->printk_enable_flag));
        printk("buf[0]=%d, buf[1]=%d\n", buf[0], buf[1]);
        return sizeof(ts_com->printk_enable_flag);
    }else if (!strcmp(attr->attr.name, "FWVersion")) {
			if(ts_com->read_version)
				ts_com->read_version(buf);
				return strlen(buf);
		}
    return 0;
}

ssize_t touch_write(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	printk("buf[0]=%d, buf[1]=%d\n", buf[0], buf[1]);
	if (!strcmp(attr->attr.name, "PrintkFlag")) {
		if (buf[0] == '0') ts_com->printk_enable_flag = 0;
		if (buf[0] == '1') ts_com->printk_enable_flag = 1;
  }	else if(!strcmp(attr->attr.name, "HardwareReset")) {
			if(ts_com->hardware_reset)
				ts_com->hardware_reset(ts_com);
  } else if (!strcmp(attr->attr.name, "SoftwareReset")) {
			if(ts_com->software_reset)
				ts_com->software_reset(ts_com);
  } else if (!strcmp(attr->attr.name, "EnableIrq")) {
			if (buf[0] == '0') {
				printk("%s: disable irq %d\n", ts_com->owner, ts_com->irq);
				disable_irq_nosync(ts_com->irq);
			}else if (buf[0] == '1') {
				printk("%s: enable irq %d\n", ts_com->owner, ts_com->irq);
				enable_irq(ts_com->irq);
			}
	} else if (!strcmp(attr->attr.name, "FWVersion")) {
			if(ts_com->read_version)
				ts_com->read_version(NULL);
	} else if (!strcmp(attr->attr.name, "upgrade")) {
			if(ts_com->upgrade_touch) {
				disable_irq_nosync(ts_com->irq);
				ts_com->upgrade_touch();
				enable_irq(ts_com->irq);
			}
	}
	return count;
}

DEVICE_ATTR(PrintkFlag, S_IRUGO | S_IWUSR, touch_read, touch_write);
DEVICE_ATTR(HardwareReset, S_IRUGO | S_IWUSR, touch_read, touch_write);
DEVICE_ATTR(SoftwareReset, S_IRUGO | S_IWUSR, touch_read, touch_write);
DEVICE_ATTR(EnableIrq, S_IRUGO | S_IWUSR, touch_read, touch_write);
DEVICE_ATTR(FWVersion, S_IRUGO | S_IWUSR, touch_read, touch_write);
DEVICE_ATTR(upgrade, S_IRUGO | S_IWUSR, touch_read, touch_write);
struct attribute *touch_attr[] = {
    &dev_attr_PrintkFlag.attr,
    &dev_attr_HardwareReset.attr,
    &dev_attr_SoftwareReset.attr,
    &dev_attr_EnableIrq.attr,
    &dev_attr_FWVersion.attr,
    &dev_attr_upgrade.attr,
    NULL
};
struct attribute_group touch_attr_group = {
    .name = NULL,
    .attrs = touch_attr,
};

int upgrade_open(struct inode * inode, struct file * filp)
{
	return 0;
}
int upgrade_close(struct inode *inode, struct file *file)
{
	return 0;
}

long upgrade_ioctl(struct file *filp,
                     unsigned int cmd, unsigned long args)
{
	switch (cmd) {			
		case 1:
		if(ts_com->upgrade_touch)
			ts_com->upgrade_touch();
			break;
		default:
			printk("%s Warning: Wrong command code\n", __func__);
			return 0;
	}
	return 0;
}
struct file_operations upgrade_fops = {
    .owner	= THIS_MODULE,
    .open	= upgrade_open,
    .release	= upgrade_close,
    .unlocked_ioctl	= upgrade_ioctl,
};

int upgrade_suspend(struct device *dev, pm_message_t state)
{
		return 0;
}

int upgrade_resume(struct device *dev)
{
	return 0;
}

struct class upgrade_class = {
    
	.name = UPGRADE_TOUCH,
	.owner = THIS_MODULE,
	.suspend = upgrade_suspend,
	.resume = upgrade_resume,
};

void set_reset_pin(struct touch_pdata *pdata, u8 on)
{
  aml_gpio_direction_output(pdata->gpio_reset, on);
  printk("%s: set gpio_reset(%d) to %d\n", pdata->owner, pdata->gpio_reset, on);
  return;
}

void set_power_pin(struct touch_pdata *pdata, u8 on)
{
  aml_gpio_direction_output(pdata->gpio_power, on);
  printk("%s: set gpio_power(%d) to %d\n", pdata->owner, pdata->gpio_power, on);
  return;
}

GET_DT_ERR_TYPE get_dt_data(struct device_node* of_node, struct touch_pdata *pdata)
{
		const char *str;
	  int err, retry, i;
		int irq_table[4] = {
												GPIO_IRQ_HIGH,
												GPIO_IRQ_LOW,
												GPIO_IRQ_RISING,
												GPIO_IRQ_FALLING,
											};
		const char *irq_string[] = {
												"GPIO_IRQ_HIGH",
												"GPIO_IRQ_LOW",
												"GPIO_IRQ_RISING",
												"GPIO_IRQ_FALLING",
											};
	if (pdata->fw_file)
		strcpy(pdata->fw_file, "/system/etc/touch/touch.fw");
	if (pdata->config_file)
		strcpy(pdata->config_file, "/system/etc/touch/touch.cfg");
	if (!of_node) {
		printk("%s: dev.of_node == NULL!\n", pdata->owner);
		return ERR_NO_NODE;
	}

  err = of_property_read_string(of_node, "touch_name", (const char **)&pdata->owner);
	if (err) {
		pdata->owner = "amlogic";
		printk("info: set name amlogic!\n");
	}
	err = of_property_read_u32(of_node,"reg",&pdata->reg);
	if (err) {
	  printk("%s info: get ic type!\n", pdata->owner);
	  pdata->reg = 0;
  }
  printk("%s: reg=%x\n", pdata->owner, pdata->reg);
	err = of_property_read_string(of_node, "i2c_bus", &str);
	if (err) {
		printk("%s info: get i2c_bus str,use default i2c bus!\n", pdata->owner);
		pdata->bus_type = AML_I2C_BUS_A;
	}
	 else {
			if (!strncmp(str, "i2c_bus_a", 9))
				pdata->bus_type = AML_I2C_BUS_A;
			else if (!strncmp(str, "i2c_bus_b", 9))
				pdata->bus_type = AML_I2C_BUS_B;
			else if (!strncmp(str, "i2c_bus_ao", 10))
				pdata->bus_type = AML_I2C_BUS_AO;
			else
				pdata->bus_type = AML_I2C_BUS_A; 
	}
	printk("%s: bus_type=%d\n", pdata->owner, pdata->bus_type);
	err = of_property_read_u32(of_node,"ic_type",&pdata->ic_type);
	if (err) {
	  printk("%s info: get ic type!\n", pdata->owner);
	  pdata->ic_type = 0;
  }
	printk("%s: IC type=%d\n", pdata->owner, pdata->ic_type);

	err = of_property_read_u32(of_node,"irq",&pdata->irq);
	if (err) {
	  printk("%s: get IRQ number!\n", pdata->owner);
		pdata->irq = 0;
  }
  pdata->irq += INT_GPIO_0;
	printk("%s: IRQ number=%d\n",pdata->owner, pdata->irq);
		
	err = of_property_read_string(of_node,"irq_edge",&str);
	if (err) {
	  printk("%s info: get irq edge, set irq edge GPIO_IRQ_FALLING!\n", pdata->owner);
	  pdata->irq_edge = irq_table[3];
  }
  else {
	  for (retry=0; retry<4; retry++) {
			if (!strcmp(str, irq_string[retry]))
				break;
		}
		if (retry == 4) {
			printk("%s info: get irq edge, set irq edge GPIO_IRQ_FALLING!\n", pdata->owner);
			pdata->irq_edge = irq_table[3];
		}
		else
			pdata->irq_edge = irq_table[retry];
	}
	err = of_property_read_u32(of_node,"auto_update_fw",&pdata->auto_update_fw);
	if (err) {
	  printk("%s info: get auto_update_fw!\n", pdata->owner);
	  pdata->auto_update_fw = 0;
  }
	err = of_property_read_u32(of_node,"xres",&pdata->xres);
	if (err) {
	  printk("%s info: get x resolution!\n", pdata->owner);
	  return ERR_GET_DATA;
  }
	err = of_property_read_u32(of_node,"yres",&pdata->yres);
	if (err) {
	  printk("%s info: get y resolution!\n",pdata->owner);
	  return ERR_GET_DATA;
  }	
	err = of_property_read_u32(of_node,"pol",&pdata->pol);
	if (err) {
	  printk("%s info: get pol!\n", pdata->owner);
	  pdata->pol = 0;
  }

	err = of_property_read_u32(of_node,"max_num",&pdata->max_num);
	if (err) {
	  printk("%s info: get max num, set max num 5!\n", pdata->owner);
	  pdata->max_num = 5;
  }

	printk("%s: xres=%d, yres=%d, pol=0x%x, max_num=%d\n",pdata->owner,pdata->xres,pdata->yres,pdata->pol,pdata->max_num);

	err = of_property_read_string(of_node, "gpio_interrupt", &str);
	if (err) {
	  printk("%s info: get gpio interrupt!\n", pdata->owner);
	  pdata->gpio_interrupt = 0;
	  return ERR_GET_DATA;
  }
  else {
    pdata->gpio_interrupt = amlogic_gpio_name_map_num(str);
    printk("%s: alloc gpio_interrupt(%s)!\n", pdata->owner, str);
    if (pdata->gpio_interrupt <= 0) {
      pdata->gpio_interrupt = 0;
      printk("%s info: alloc gpio_interrupt(%s)!\n", pdata->owner, str);
      //return ERR_GPIO_REQ;
    }
  }

	err = of_property_read_string(of_node, "gpio_reset", &str);
	if (!err){
    pdata->gpio_reset = amlogic_gpio_name_map_num(str);
    printk("%s: alloc gpio_reset(%s)!\n", pdata->owner, str);
    if (pdata->gpio_reset <= 0) {
    	pdata->gpio_reset = 0;
		printk("%s info: alloc gpio_reset(%s)!\n", pdata->owner, str);
    }
  }
  else {
  	pdata->gpio_reset = 0;
  }

	err = of_property_read_string(of_node, "gpio_power", &str);
	if (!err){
    pdata->gpio_power = amlogic_gpio_name_map_num(str);
    printk("%s: alloc gpio_power(%s)!\n", pdata->owner, str);
    if (pdata->gpio_power <= 0) {
    	pdata->gpio_power = 0;
		printk("%s info: alloc gpio_power(%s)!\n", pdata->owner, str);
    }
  }
  else {
  	pdata->gpio_power = 0;
  }

  err = of_property_read_string(of_node,"fw_file",&str);
	if (err) {
	  printk("%s info: get fw_file, set firmware %s!\n",pdata->owner, pdata->fw_file);
  }
  else {
	strcpy(pdata->fw_file, str);
  	printk("%s get fw_file, set firmware %s!\n",pdata->owner, pdata->fw_file);
  }

  err = of_property_read_string(of_node,"config_file", &str);
	if (err) {
	  printk("%s info: get config_file, set config_file %s!\n", pdata->owner, pdata->config_file);
  }
  else {
	strcpy(pdata->config_file, str);
  	printk("%s get config_file, set config_file %s!\n", pdata->owner, pdata->config_file);
  }
  err = of_property_read_u32(of_node,"select_gpio_num",&pdata->select_gpio_num);
  if (err) {
	 printk("%s info: get select_gpio_num!\n", pdata->owner);
	 pdata->select_gpio_num = 0;
  }
  if (pdata->select_gpio_num > 0) {
	for (i=0; i<pdata->select_gpio_num; i++) {
	  err = of_property_read_string_index(of_node, "select_fw_gpio", i, &str);
	  if(err < 0){
		printk("%s: find select_fw_gpio[%d] faild\n", pdata->owner, i);
		break;
	  }
	  else {
		   pdata->select_fw_gpio[i] = amlogic_gpio_name_map_num(str);
		   printk("%s: alloc select_fw_gpio[%d](%s)!\n", pdata->owner, i, str);
		   if (pdata->select_fw_gpio[i] <= 0) {
		      pdata->select_fw_gpio[i] = 0;
		      printk("%s info: alloc select_fw_gpio[%d](%s)!\n", pdata->owner, i, str);
		      //return ERR_GPIO_REQ;
		   }
	  }
	}
 }
  return ERR_NO;
}

int create_init(struct device dev, struct touch_pdata *pdata)
{
	int err;
  err = sysfs_create_group(&dev.kobj, &touch_attr_group);
  err = alloc_chrdev_region(&pdata->upgrade_no, 0, 1, UPGRADE_TOUCH);
  if (err < 0) {
      printk("Can't register major for upgrade_touch device\n");
  }
	/* connect the file operations with cdev */
	cdev_init(&pdata->upgrade_cdev, &upgrade_fops);
	pdata->upgrade_cdev.owner = THIS_MODULE;

	/* connect the major/minor number to the cdev */
	err = cdev_add(&pdata->upgrade_cdev, pdata->upgrade_no, 1);
	if (err) {
		printk("upgrade touch: failed to add device. \n");
	}
	err = class_register(&upgrade_class);
	if (err < 0) {
		printk("class_register(&upgrade_class) failed!\n");
	}
	pdata->dev = device_create(&upgrade_class, NULL, pdata->upgrade_no, NULL, UPGRADE_TOUCH);
	if (IS_ERR(pdata->dev)) {
	 	printk(KERN_ERR "upgrade_cdev: failed to create device node\n");
	  class_destroy(&upgrade_class);
    return -EEXIST;
	}
	
	return 0;
}

GET_DT_ERR_TYPE request_touch_gpio(struct touch_pdata *pdata)
{
	int err, i;
	 
	if (pdata->gpio_interrupt) {
      err = aml_gpio_request(pdata->gpio_interrupt);
      if (err) {
        printk("%s: faild to alloc gpio_interrupt!\n", pdata->owner);
        return ERR_GPIO_REQ;
      }
	    printk("%s: request gpio_interrupt = (%d)\n",pdata->owner, pdata->gpio_interrupt);
	    aml_gpio_direction_input(pdata->gpio_interrupt);
	    aml_gpio_to_irq(pdata->gpio_interrupt, pdata->irq-INT_GPIO_0, pdata->irq_edge);
	}
	if (pdata->gpio_reset) {
      err = aml_gpio_request(pdata->gpio_reset);
      if (err) {
        printk("%s: faild to alloc gpio_reset!\n", pdata->owner);
        return ERR_GPIO_REQ;
      }
	    printk("%s: request gpio_reset = (%d)\n",pdata->owner, pdata->gpio_reset);
	}
	if (pdata->gpio_power) {
      err = aml_gpio_request(pdata->gpio_power);
      if (err) {
        printk("%s: faild to alloc gpio_power!\n", pdata->owner);
        return ERR_GPIO_REQ;
      }
	    printk("%s: request gpio_power = (%d)\n",pdata->owner, pdata->gpio_power);
	}
	
	if (pdata->select_gpio_num) {
		for (i=0; i<pdata->select_gpio_num; i++)
			if (pdata->select_fw_gpio[i] > 0) {
				err = aml_gpio_request(pdata->select_fw_gpio[i]);
				if (err) {
					printk("%s: faild to alloc select_fw_gpio[%d]!\n", pdata->owner, i);
					return ERR_GPIO_REQ;
				}
				aml_gpio_direction_input(pdata->select_fw_gpio[i]);
				printk("%s: request select_fw_gpio[%d] = (%d)\n",pdata->owner, i, pdata->select_fw_gpio[i]);
		  }
	}

	return ERR_NO;
}

void free_touch_gpio(struct touch_pdata *pdata)
{
	int i;
	if (pdata->gpio_interrupt) {
		aml_gpio_free(pdata->gpio_interrupt);
		pdata->gpio_interrupt = 0;
	}
	if (pdata->gpio_reset) {
		aml_gpio_free(pdata->gpio_reset);
		pdata->gpio_reset = 0;
	}
	if (pdata->gpio_power) {
		aml_gpio_free(pdata->gpio_power);
		pdata->gpio_power = 0;
	}

	if (pdata->select_gpio_num) {
		for (i=0; i<pdata->select_gpio_num; i++) {
			aml_gpio_free(pdata->select_fw_gpio[i]);
			pdata->select_fw_gpio[i] = 0;
		}
	}
}

int get_gpio_fw(struct touch_pdata *pdata)
{
	int value = 0, i;
	if (pdata->select_gpio_num <= 0) {
		printk("%s: pdata->select_gpio_num = %d\n", pdata->owner, pdata->select_gpio_num);
		return -1;
	}
	for (i=0; i<pdata->select_gpio_num; i++)
		value |= aml_get_value(pdata->select_fw_gpio[i])<<i;
	value = value & ((2 << pdata->select_gpio_num) - 1);
	printk("%s: get_gpio_fw = 0x%x\n",pdata->owner, value);
	return value;
}
void destroy_remove(struct device dev, struct touch_pdata *pdata)
{
	sysfs_remove_group(&dev.kobj, &touch_attr_group);
	cdev_del(&pdata->upgrade_cdev);
	device_destroy(&upgrade_class, pdata->upgrade_no);
	class_destroy(&upgrade_class);
	free_touch_gpio(pdata);
}

static ssize_t touch_class_read(struct class *cla, struct class_attribute *attr, char *buf)
{
    if (!strcmp(attr->attr.name, "PrintkFlag")) {
        memcpy(buf, &ts_com->printk_enable_flag,sizeof(ts_com->printk_enable_flag));
        printk("buf[0]=%d, buf[1]=%d\n", buf[0], buf[1]);
        return sizeof(ts_com->printk_enable_flag);
    }else if (!strcmp(attr->attr.name, "FWVersion")) {
			if(ts_com->read_version)
				ts_com->read_version(buf);
				return strlen(buf);
		}else if (!strcmp(attr->attr.name, "pol")) {
			sprintf(buf, "%x\n", ts_com->pol);
			printk("ts_com->pol = 0x%x\n", ts_com->pol);
			return sizeof(ts_com->pol);
		}
    return 0;
}
static ssize_t touch_class_write(struct class *cla, struct class_attribute *attr, const char *buf, size_t count)
{
	printk("buf[0]=%d, buf[1]=%d\n", buf[0], buf[1]);
	if (!strcmp(attr->attr.name, "PrintkFlag")) {
		if (buf[0] == '0') ts_com->printk_enable_flag = 0;
		if (buf[0] == '1') ts_com->printk_enable_flag = 1;
  }	else if(!strcmp(attr->attr.name, "HardwareReset")) {
			if(ts_com->hardware_reset)
				ts_com->hardware_reset(ts_com);
  } else if (!strcmp(attr->attr.name, "SoftwareReset")) {
			if(ts_com->software_reset)
				ts_com->software_reset(ts_com);
  } else if (!strcmp(attr->attr.name, "EnableIrq")) {
			if (buf[0] == '0') {
				printk("%s: disable irq %d\n", ts_com->owner, ts_com->irq);
				disable_irq_nosync(ts_com->irq);
			}else if (buf[0] == '1') {
				printk("%s: enable irq %d\n", ts_com->owner, ts_com->irq);
				enable_irq(ts_com->irq);
			}
	} else if (!strcmp(attr->attr.name, "FWVersion")) {
			if(ts_com->read_version)
				ts_com->read_version(NULL);
	} else if (!strcmp(attr->attr.name, "upgrade")) {
			if(ts_com->upgrade_touch) {
				disable_irq_nosync(ts_com->irq);
				ts_com->upgrade_touch();
				enable_irq(ts_com->irq);
			}
	} else if (!strcmp(attr->attr.name, "pol")) {
		  sscanf(buf, "%x", &ts_com->pol);
			printk("ts_com->pol = 0x%x\n", ts_com->pol);
	}
	return count;
}
static struct class_attribute touch_class_attrs[] = {
    __ATTR(PrintkFlag, S_IRUGO | S_IWUSR, touch_class_read, touch_class_write),
    __ATTR(HardwareReset, S_IRUGO | S_IWUSR, touch_class_read, touch_class_write),
    __ATTR(SoftwareReset, S_IRUGO | S_IWUSR, touch_class_read, touch_class_write),
    __ATTR(EnableIrq, S_IRUGO | S_IWUSR, touch_class_read, touch_class_write),
    __ATTR(FWVersion, S_IRUGO | S_IWUSR, touch_class_read, touch_class_write),
    __ATTR(upgrade, S_IRUGO | S_IWUSR, touch_class_read, touch_class_write),
    __ATTR(pol, S_IRUGO | S_IWUSR, touch_class_read, touch_class_write),
    __ATTR_NULL
};
static struct class touch_class = {
    .name = "aml_touch",
    .class_attrs = touch_class_attrs,
};

static int touch_ts_probe(struct platform_device *pdev)
{
	struct device_node* touch_node = pdev->dev.of_node;
	struct device_node* child;
	struct i2c_board_info board_info;
	struct i2c_adapter *adapter;
	struct i2c_client *i2c_client;
	int i = 0;
	struct touch_pdata *pdata = NULL;
	printk("==%s==start==\n", __func__);
	if (!pdata)
		pdata = kzalloc(sizeof(*pdata)*MAX_TOUCH, GFP_KERNEL);
	if (!pdata)	{
		printk("fail alloc data!\n");
		goto exit_alloc_data_failed;
	}
	pdev->dev.platform_data = pdata;
	for_each_child_of_node(touch_node, child) {
		if (get_dt_data(child, pdata+i) != ERR_NO) {
			printk("fail get dt data!\n");
			goto exit_alloc_data_failed;
		}
		adapter = i2c_get_adapter((pdata+i)->bus_type);
		if (!adapter) {
			printk("warnning£ºfail get adapter!\n");
			continue;
		}
		memset(&board_info, 0, sizeof(board_info));
		strncpy(board_info.type, (pdata+i)->owner, I2C_NAME_SIZE);
		board_info.addr = (pdata+i)->reg;
		board_info.platform_data = (pdata+i);
		//printk("%s: adapter = %d\n", (pdata+i)->owner, adapter);
		i2c_client = i2c_new_device(adapter, &board_info);
		i2c_client->irq = (pdata+i)->irq;
		if (!i2c_client) {
			printk("%s :fail new i2c device\n", (pdata+i)->owner);
			goto exit_alloc_data_failed;
		}
		else{
			printk("%s: new i2c device successed\n",((struct touch_pdata *)(i2c_client->dev.platform_data))->owner);
			//printk("pdata addr = %x\n", pdata+i);
		}
		i ++;
		if (i >= MAX_TOUCH) {
			printk("warnning: touch num out of range max touch num(%d)\n", MAX_TOUCH);
			break;
		}
	}
	class_register(&touch_class);
	printk("==%s==end==\n", __func__);
	return 0;
	
exit_alloc_data_failed:
	if (pdata)
		kfree(pdata);
	return -1;
}

static int touch_ts_remove(struct platform_device *pdev)
{
	if (pdev->dev.platform_data)
	 	kfree (pdev->dev.platform_data);
	class_unregister(&touch_class);
    return 0;
}

static const struct of_device_id aml_touch_dt_match[]={
	{	
		.compatible = "amlogic,aml_touch",
	},
	{},
};

static  struct platform_driver aml_touch_driver = {
	.probe		= touch_ts_probe,
	.remove		= touch_ts_remove,
	.driver		= {
		.name	= "aml_touch",
		.owner	= THIS_MODULE,
		.of_match_table = aml_touch_dt_match,
	},
};

static int touch_ts_init(void)
{
	int ret;
	printk("==%s==\n", __func__);
	ret = platform_driver_register(&aml_touch_driver);
	return ret;
}

/*******************************************************
Description:
	Driver uninstall function.
return:
	Executive Outcomes. 0---succeed.
********************************************************/
static void __exit touch_ts_exit(void)
{
	platform_driver_unregister(&aml_touch_driver);		//release our work queue
}

module_init(touch_ts_init);
module_exit(touch_ts_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Amlogic Touch common driver");
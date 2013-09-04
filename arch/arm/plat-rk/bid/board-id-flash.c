#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/circ_buf.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <mach/iomux.h>
#include <mach/gpio.h>
#include <asm/gpio.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>


#include <linux/file.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/debugfs.h>
#include <linux/mempolicy.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/cacheflush.h>
#include <linux/fb.h>
#include <plat/ipp.h>


#include <linux/board-id.h>


extern struct board_id_private_data *g_board_id;

#if defined(CONFIG_BOARD_ID_AUTO_XML)

#define	MAX_BUF_LEN	200
static ssize_t board_id_proc_write(struct file *file, const char __user *buffer,
			   size_t count, loff_t *data)
{
	char c;
	int rc;
	int i = 0, num = 0, j = 0;
	struct board_id_private_data *board_id = g_board_id;
	char buf[MAX_BUF_LEN];
	struct device_id_name  *device_id_name_start = board_id->tp_id_name;	
	struct area_id_name  *area_id_name_start = board_id->area_area_id_name;
	int max_id = TP_ID_NUMS + LCD_ID_NUMS + KEY_ID_NUMS + CODEC_ID_NUMS + WIFI_ID_NUMS + BT_ID_NUMS + GPS_ID_NUMS + FM_ID_NUMS + MODEM_ID_NUMS + DDR_ID_NUMS + FLASH_ID_NUMS + HDMI_ID_NUMS
		+ BATTERY_ID_NUMS + CHARGE_ID_NUMS + BACKLIGHT_ID_NUMS + HEADSET_ID_NUMS + MICPHONE_ID_NUMS + SPEAKER_ID_NUMS + VIBRATOR_ID_NUMS + TV_ID_NUMS
		+ ECHIP_ID_NUMS + HUB_ID_NUMS + TPAD_ID_NUMS + PMIC_ID_NUMS + REGULATOR_ID_NUMS + RTC_ID_NUMS + CAMERA_FRONT_ID_NUMS + CAMERA_BACK_ID_NUMS + ANGLE_ID_NUMS + ACCEL_ID_NUMS
		+ COMPASS_ID_NUMS + GYRO_ID_NUMS + LIGHT_ID_NUMS + PROXIMITY_ID_NUMS + TEMPERATURE_ID_NUMS + PRESSURE_ID_NUMS;
	
	rc = get_user(c, buffer);
	if (rc)
	{
		atomic_set(&board_id->flags.debug_flag, 0);
		return rc; 
	}

	
	num = c - '0';

	printk("%s command list:debug close:0,debug enable:1, create /data/board-id-data.txt :2, create /data/board-id-cust.xml:3, create /data/board-id-device.xml :4\n",__func__);

	switch(num)
	{
		case 0:
		case 1:			
		if(board_id)
		atomic_set(&board_id->flags.debug_flag, num);
		break;

		case 2:	
		printk("%s:create /data/board-id-data.txt file\n",__func__);
		if(!board_id->board_id_data_filp)
		board_id->board_id_data_filp = filp_open("/data/board-id-data.txt",O_CREAT|O_TRUNC|O_RDWR,0);

		if (board_id->board_id_data_filp)
		{
		  board_id->board_id_data_fs = get_fs();
		  set_fs(get_ds());
		}
		
		memset(buf, 0, MAX_BUF_LEN);
		for(i=0; i<max_id; i++)
		{			
			if(device_id_name_start->type && *device_id_name_start->dev_name)
			{
				snprintf(buf, MAX_BUF_LEN, "type=%d,id=%d,name=%s:%s,%d", device_id_name_start->type, device_id_name_start->id, device_id_name_start->dev_name, device_id_name_start->description, device_id_name_start->device_id);			
				buf[MAX_BUF_LEN-1] = '\n';
				board_id->board_id_data_filp->f_op->write(board_id->board_id_data_filp, buf, MAX_BUF_LEN, &board_id->board_id_data_filp->f_pos);
			}
			memset(buf,0,MAX_BUF_LEN);
			device_id_name_start++;
		}

		memset(buf, '\n', MAX_BUF_LEN);
		board_id->board_id_data_filp->f_op->write(board_id->board_id_data_filp, buf, 3, &board_id->board_id_data_filp->f_pos);	
		device_id_name_start = board_id->device_selected;
		memset(buf, 0, MAX_BUF_LEN);
		for(i=0; i<DEVICE_NUM_TYPES; i++)
		{			
			if(device_id_name_start->type && *device_id_name_start->dev_name)
			{
				snprintf(buf, MAX_BUF_LEN, "device_selected: type=%d,id=%d,name=%s:%s,%d", device_id_name_start->type, device_id_name_start->id, device_id_name_start->dev_name, device_id_name_start->description, device_id_name_start->device_id);			
				buf[MAX_BUF_LEN-1] = '\n';
				board_id->board_id_data_filp->f_op->write(board_id->board_id_data_filp, buf, MAX_BUF_LEN, &board_id->board_id_data_filp->f_pos);				
			}
			memset(buf, 0, MAX_BUF_LEN);
			device_id_name_start++;
		}

		memset(buf, '\n', MAX_BUF_LEN);
		board_id->board_id_data_filp->f_op->write(board_id->board_id_data_filp, buf, 3, &board_id->board_id_data_filp->f_pos);	
		area_id_name_start = board_id->area_area_id_name;
		memset(buf, 0, MAX_BUF_LEN);
		for(i=AREA_ID_NULL; i<AREA_ID_NUMS+1; i++)
		{			
			if(area_id_name_start->type && (strlen(area_id_name_start->locale_language) > 0))
			{
				if(i == AREA_ID_NUMS)
				snprintf(buf, MAX_BUF_LEN, "area_selected: type=%d,id=%d,name=%s_%s", area_id_name_start->type, area_id_name_start->id, area_id_name_start->locale_language, area_id_name_start->locale_region);
				else
				snprintf(buf, MAX_BUF_LEN, "type=%d,id=%d,name=%s_%s", area_id_name_start->type, area_id_name_start->id, area_id_name_start->locale_language, area_id_name_start->locale_region);			
				buf[MAX_BUF_LEN-1] = '\n';
				board_id->board_id_data_filp->f_op->write(board_id->board_id_data_filp, buf, MAX_BUF_LEN, &board_id->board_id_data_filp->f_pos);				
			}
			memset(buf, 0, MAX_BUF_LEN);
			area_id_name_start++;
		}

		
		break;

		case 3:		
	
		if(!board_id->board_id_area_filp)
		board_id->board_id_area_filp = filp_open("/data/board-id-cust.xml",O_CREAT|O_TRUNC|O_RDWR,0);

		if (board_id->board_id_area_filp)
		{
		  board_id->board_id_area_fs = get_fs();
		  set_fs(get_ds());
		}

		board_id->board_id_area_filp->f_op->write(board_id->board_id_area_filp, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n", 40, &board_id->board_id_area_filp->f_pos);

		board_id->board_id_area_filp->f_op->write(board_id->board_id_area_filp, "<cust>\n", 7, &board_id->board_id_area_filp->f_pos);	
		
		area_id_name_start = board_id->area_area_id_name;
		memset(buf, 0, MAX_BUF_LEN);
		for(i=AREA_ID_NULL; i<AREA_ID_NUMS; i++)
		{						
			if(area_id_name_start->type && (strlen(area_id_name_start->locale_language) > 0) && (strlen(area_id_name_start->locale_region) > 0))
			
			{
				memset(buf, 0, MAX_BUF_LEN);
				snprintf(buf, MAX_BUF_LEN, "\t<if tid=\"%d\" bid=\"%d\" area=\"%s\">\n",area_id_name_start->type, area_id_name_start->id, area_id_name_start->country_area);					
				board_id->board_id_area_filp->f_op->write(board_id->board_id_area_filp, buf, MAX_BUF_LEN, &board_id->board_id_area_filp->f_pos);

				memset(buf, 0, MAX_BUF_LEN);
				snprintf(buf, MAX_BUF_LEN, "\t\t<set name=\"persist.sys.language\" value=\"%s\"/>\n\t\t<set name=\"persist.sys.country\" value=\"%s\"/>\n\t\t<set name=\"persist.sys.timezone\" value=\"%s\"/>\n", area_id_name_start->locale_language, area_id_name_start->locale_region, area_id_name_start->timezone);
				board_id->board_id_area_filp->f_op->write(board_id->board_id_area_filp, buf, MAX_BUF_LEN, &board_id->board_id_area_filp->f_pos);


				memset(buf, 0, MAX_BUF_LEN);
				snprintf(buf, MAX_BUF_LEN, "\t\t<set name=\"ro.product.locale.language\" value=\"%s\"/>\n\t\t<set name=\"ro.product.locale.region\" value=\"%s\"/>\n", area_id_name_start->locale_language, area_id_name_start->locale_region);
				board_id->board_id_area_filp->f_op->write(board_id->board_id_area_filp, buf, MAX_BUF_LEN, &board_id->board_id_area_filp->f_pos);
				//memset(buf, 0, MAX_BUF_LEN);
				//snprintf(buf, MAX_BUF_LEN, "\t\t<cp src=\"cust/app/area.apk\" des=\"system/app/area.apk\" mode=\"0644\"/>\n \t\t<rm src=\"system/app/area.apk\" />\n");					
				//board_id->board_id_area_filp->f_op->write(board_id->board_id_area_filp, buf, MAX_BUF_LEN, &board_id->board_id_area_filp->f_pos);
				for(j=0; j<GMS_ID_NUMS; j++)
				{
					if(xml_config[i].gms_flag[j])
					{
						memset(buf, 0, MAX_BUF_LEN);
						snprintf(buf, MAX_BUF_LEN, "\t\t<rm src=\"cust/app/%s\" des=\"system/app/%s\" mode=\"0644\"/>\n", gms_name[j].gms_name, gms_name[j].gms_name);	
						board_id->board_id_area_filp->f_op->write(board_id->board_id_area_filp, buf, MAX_BUF_LEN, &board_id->board_id_area_filp->f_pos);
					}
				}
				
				//</if>		
				memset(buf, 0, MAX_BUF_LEN);
				snprintf(buf, MAX_BUF_LEN, "\t</if>\n");					
				board_id->board_id_area_filp->f_op->write(board_id->board_id_area_filp, buf, MAX_BUF_LEN, &board_id->board_id_area_filp->f_pos);
				
			}
		
			memset(buf, 0, MAX_BUF_LEN);
			if(i < AREA_ID_NUMS)
			area_id_name_start++;
			
		}
		
		memset(buf, 0, MAX_BUF_LEN);
		snprintf(buf, MAX_BUF_LEN, "\t<if area=\"!China\" language=\"!zh\" local=\"!CN\">\n");
		board_id->board_id_area_filp->f_op->write(board_id->board_id_area_filp, buf, MAX_BUF_LEN, &board_id->board_id_area_filp->f_pos);

		memset(buf, 0, MAX_BUF_LEN);
		snprintf(buf, MAX_BUF_LEN, "\t\t<cp src=\"cust/app/area.apk\" des=\"system/app/area.apk\" mode=\"0644\"/>\n \t\t<rm src=\"system/app/area.apk\" />\n\t</if>\n");					
		board_id->board_id_area_filp->f_op->write(board_id->board_id_area_filp, buf, MAX_BUF_LEN, &board_id->board_id_area_filp->f_pos);	

		board_id->board_id_area_filp->f_op->write(board_id->board_id_area_filp, "</cust>\n", 8, &board_id->board_id_area_filp->f_pos);
		
		printk("%s:/data/board-id-cust.xml file\n",__func__);
		break;


		case 4:			
		if(!board_id->board_id_device_filp)
		board_id->board_id_device_filp = filp_open("/data/board-id-device.xml",O_CREAT|O_TRUNC|O_RDWR,0);

		if (board_id->board_id_device_filp)
		{
		  board_id->board_id_device_fs = get_fs();
		  set_fs(get_ds());
		}
		
		board_id->board_id_device_filp->f_op->write(board_id->board_id_device_filp, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n", 40, &board_id->board_id_device_filp->f_pos);

		board_id->board_id_device_filp->f_op->write(board_id->board_id_device_filp, "<device>\n", 9, &board_id->board_id_device_filp->f_pos);	
		
		device_id_name_start = board_id->tp_id_name;
		memset(buf, 0, MAX_BUF_LEN);

		for(i=DEVICE_TYPE_TP; i<DEVICE_NUM_TYPES; i++)
		{			
			device_id_name_start = board_id->device_start_addr[i];
			
			device_id_name_start = board_id->tp_id_name;
			
			for(j=0; j<max_id; j++)
			{			
				if((device_id_name_start->type == i) && (strlen(device_id_name_start->dev_name) > 0))
				{

					memset(buf, 0, MAX_BUF_LEN);
					snprintf(buf, MAX_BUF_LEN, "\t<if tid=\"%d\" bid=\"%d\" type=\"%s\" dev=\"%s\" desc=\"%s\">\n", device_id_name_start->type,  device_id_name_start->id, device_id_name_start->type_name, device_id_name_start->dev_name, device_id_name_start->description);					
					board_id->board_id_device_filp->f_op->write(board_id->board_id_device_filp, buf, MAX_BUF_LEN, &board_id->board_id_device_filp->f_pos);

					if(DEVICE_TYPE_KEY == device_id_name_start->type)
					{
						memset(buf, 0, MAX_BUF_LEN);   //added by luodh
						snprintf(buf, MAX_BUF_LEN, "\t\t<set name=\"ro.product.locale.keylayout\" value=\"%s\"/>\n",  device_id_name_start->dev_name);
						board_id->board_id_device_filp->f_op->write(board_id->board_id_device_filp, buf, MAX_BUF_LEN, &board_id->board_id_device_filp->f_pos);	
					}
					memset(buf, 0, MAX_BUF_LEN);
					snprintf(buf, MAX_BUF_LEN, "\t\t<cp src=\"cust/so/type.dev.so\" des=\"system/lib/hw/type.dev.so\" mode=\"0644\"/>\n \t\t<rm src=\"system/lib/hw/type.dev.so\"/>\n");					
					board_id->board_id_device_filp->f_op->write(board_id->board_id_device_filp, buf, MAX_BUF_LEN, &board_id->board_id_device_filp->f_pos);	

					memset(buf, 0, MAX_BUF_LEN);
					snprintf(buf, MAX_BUF_LEN, "\t</if>\n");
					board_id->board_id_device_filp->f_op->write(board_id->board_id_device_filp, buf, MAX_BUF_LEN, &board_id->board_id_device_filp->f_pos);

					memset(buf, 0, MAX_BUF_LEN);

					//printk("%s:type=%d,id=%d\n",__func__,device_id_name_start->type, device_id_name_start->id);
					
				}	
				
				device_id_name_start++; 

			}

			memset(buf, 0, MAX_BUF_LEN);

		}	
				
		//board_id->board_id_device_filp->f_op->write(board_id->board_id_device_filp, "\t</type>\n", 9, &board_id->board_id_device_filp->f_pos);
				
		board_id->board_id_device_filp->f_op->write(board_id->board_id_device_filp, "</device>\n", 10, &board_id->board_id_device_filp->f_pos);
		
		printk("%s:/data/board-id-device.xml file\n",__func__);
		break;

		default:
		break;

	}
		
	printk("%s %d\n", __func__, __LINE__);
	return count; 
}

static const struct file_operations board_id_proc_fops = {
	.owner		= THIS_MODULE, 
	.write		= board_id_proc_write,
};

#endif

static int board_id_open(struct inode *inode, struct file *file)
{
	struct board_id_private_data *board_id = g_board_id;
	
	return 0;
}

static int board_id_release(struct inode *inode, struct file *file)
{	
	struct board_id_private_data *board_id = g_board_id;

	return 0;
}

static struct device_id_name device_selected[DEVICE_NUM_TYPES]; 

static long board_id_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct board_id_private_data *board_id = g_board_id;
	void __user *argp = (void __user *)arg;
	struct area_id_name area_select = board_id->area_select;
	struct operator_id_name operator_select = board_id->operator_select;
	struct reserve_id_name reserve_select = board_id->reserve_select;
	struct device_id_name device_selected_temp, device_selected_last_temp;
	struct area_id_name language_last_select;	
	struct operator_id_name operator_last_select;
	struct reserve_id_name reserve_last_select;
	int result = 0;
	int i = 0;
	switch(cmd)
	{
		case BOARD_ID_IOCTL_READ_AREA_ID:
			mutex_lock(&board_id->operation_mutex);
			if(copy_to_user(argp, &area_select, sizeof(struct area_id_name)))
			{
				printk("%s:fail to copy area_id_name to user\n",__func__);
				mutex_unlock(&board_id->operation_mutex);
				return -EFAULT;
			}
			DBG_ID("%s:BOARD_ID_IOCTL_READ_LANGUAGE_ID:%s_%s\n",__func__,area_select.locale_language,area_select.locale_region);
			mutex_unlock(&board_id->operation_mutex);
			break;

		case BOARD_ID_IOCTL_READ_OPERATOR_ID:
			mutex_lock(&board_id->operation_mutex);
			if(copy_to_user(argp, &operator_select, sizeof(struct operator_id_name)))
			{
				printk("%s:fail to copy operator_id_name to user\n",__func__);
				mutex_unlock(&board_id->operation_mutex);
				return -EFAULT;
			}
			DBG_ID("%s:BOARD_ID_IOCTL_READ_operator_ID:%s_%s\n",__func__,operator_select.operator_name,operator_select.locale_region);
			mutex_unlock(&board_id->operation_mutex);
			break;

		case BOARD_ID_IOCTL_READ_RESERVE_ID:
			mutex_lock(&board_id->operation_mutex);
			if(copy_to_user(argp, &reserve_select, sizeof(struct reserve_id_name)))
			{
				printk("%s:fail to copy reserve_id_name to user\n",__func__);
				mutex_unlock(&board_id->operation_mutex);
				return -EFAULT;
			}
			DBG_ID("%s:BOARD_ID_IOCTL_READ_RESERVE_ID:%s,%s\n",__func__,reserve_select.reserve_name, reserve_select.locale_region);
			mutex_unlock(&board_id->operation_mutex);
			break;

		case BOARD_ID_IOCTL_READ_STATUS:
			mutex_lock(&board_id->operation_mutex);
			if(copy_to_user(argp, &board_id->vendor_data[DEVICE_TYPE_STATUS], sizeof(board_id->vendor_data[DEVICE_TYPE_STATUS])))
			{
				printk("%s:line=%d:fail to copy vendor_data to user\n",__func__,__LINE__);	
				mutex_unlock(&board_id->operation_mutex);	
				return -EFAULT;
			}
			mutex_unlock(&board_id->operation_mutex);
			DBG_ID("%s:BOARD_ID_IOCTL_READ_STATUS=0x%x\n",__func__,board_id->vendor_data[DEVICE_TYPE_STATUS]);
			break;

		case BOARD_ID_IOCTL_READ_VENDOR_DATA:	
			mutex_lock(&board_id->operation_mutex);			
			DBG_ID("%s:BOARD_ID_IOCTL_READ_VENDOR_DATA:\n",__func__);
			if(copy_to_user(argp, board_id->vendor_data, sizeof(board_id->vendor_data)))
			{
				printk("%s:line=%d:fail to copy vendor_data to user\n",__func__,__LINE__);	
				mutex_unlock(&board_id->operation_mutex);	
				return -EFAULT;
			}
			mutex_unlock(&board_id->operation_mutex);
			
			break;
		
	}
	
	mutex_lock(&board_id->operation_mutex); 
	switch(cmd)
	{	
		
		case BOARD_ID_IOCTL_READ_TP_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_TP];
			break;
		case BOARD_ID_IOCTL_READ_LCD_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_LCD];
			break;
		case BOARD_ID_IOCTL_READ_KEY_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_KEY];
			break;
		case BOARD_ID_IOCTL_READ_CODEC_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_CODEC];
			break;
		case BOARD_ID_IOCTL_READ_WIFI_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_WIFI];
			break;
		case BOARD_ID_IOCTL_READ_BT_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_BT];
			break;
		case BOARD_ID_IOCTL_READ_GPS_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_GPS];
			break;
		case BOARD_ID_IOCTL_READ_FM_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_FM];
			break;
		case BOARD_ID_IOCTL_READ_MODEM_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_MODEM];
			break;
		case BOARD_ID_IOCTL_READ_DDR_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_DDR];
			break;
		case BOARD_ID_IOCTL_READ_FLASH_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_FLASH];
			break;
		case BOARD_ID_IOCTL_READ_HDMI_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_HDMI];
			break;
		case BOARD_ID_IOCTL_READ_BATTERY_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_BATTERY];
			break;
		case BOARD_ID_IOCTL_READ_CHARGE_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_CHARGE];
			break;
		case BOARD_ID_IOCTL_READ_BACKLIGHT_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_BACKLIGHT];
			break;
		case BOARD_ID_IOCTL_READ_HEADSET_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_HEADSET];
			break;
		case BOARD_ID_IOCTL_READ_MICPHONE_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_MICPHONE];
			break;
		case BOARD_ID_IOCTL_READ_SPEAKER_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_SPEAKER];
			break;
		case BOARD_ID_IOCTL_READ_VIBRATOR_ID:	
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_VIBRATOR];
			break;
		case BOARD_ID_IOCTL_READ_TV_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_TV];
			break;
		case BOARD_ID_IOCTL_READ_ECHIP_ID:		
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_ECHIP];
			break; 
		case BOARD_ID_IOCTL_READ_HUB_ID:		
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_HUB];
			break;
		case BOARD_ID_IOCTL_READ_TPAD_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_TPAD];
			break;
		case BOARD_ID_IOCTL_READ_PMIC_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_PMIC];
			break;
		case BOARD_ID_IOCTL_READ_REGULATOR_ID:	
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_REGULATOR];
			break;
		case BOARD_ID_IOCTL_READ_RTC_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_RTC];
			break;
		case BOARD_ID_IOCTL_READ_CAMERA_FRONT_ID:	
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_CAMERA_FRONT];
			break;
		case BOARD_ID_IOCTL_READ_CAMERA_BACK_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_CAMERA_BACK];
			break;
		case BOARD_ID_IOCTL_READ_SENSOR_ANGLE_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_ANGLE];
			break;
		case BOARD_ID_IOCTL_READ_SENSOR_ACCEL_ID:	
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_ACCEL];
			break;
		case BOARD_ID_IOCTL_READ_SENSOR_COMPASS_ID:	
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_COMPASS];
			break;
		case BOARD_ID_IOCTL_READ_SENSOR_GYRO_ID :	
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_GYRO];
			break;
		case BOARD_ID_IOCTL_READ_SENSOR_LIGHT_ID:	
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_LIGHT];
			break;
		case BOARD_ID_IOCTL_READ_SENSOR_PROXIMITY_ID:
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_PROXIMITY];
			break;
		case BOARD_ID_IOCTL_READ_SENSOR_TEMPERATURE_ID: 
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_TEMPERATURE];
			break;
		case BOARD_ID_IOCTL_READ_SENSOR_PRESSURE_ID:	
			device_selected_temp = board_id->device_selected[DEVICE_TYPE_PRESSURE];
			break;
		
		case BOARD_ID_IOCTL_WRITE_TP_ID: 
		case BOARD_ID_IOCTL_WRITE_LCD_ID: 
		case BOARD_ID_IOCTL_WRITE_KEY_ID: 
		case BOARD_ID_IOCTL_WRITE_CODEC_ID: 
		case BOARD_ID_IOCTL_WRITE_WIFI_ID: 
		case BOARD_ID_IOCTL_WRITE_BT_ID: 	
		case BOARD_ID_IOCTL_WRITE_GPS_ID: 
		case BOARD_ID_IOCTL_WRITE_FM_ID: 
		case BOARD_ID_IOCTL_WRITE_MODEM_ID: 	
		case BOARD_ID_IOCTL_WRITE_DDR_ID: 
		case BOARD_ID_IOCTL_WRITE_FLASH_ID: 
		case BOARD_ID_IOCTL_WRITE_HDMI_ID: 
		case BOARD_ID_IOCTL_WRITE_BATTERY_ID: 
		case BOARD_ID_IOCTL_WRITE_CHARGE_ID:  
		case BOARD_ID_IOCTL_WRITE_BACKLIGHT_ID: 
		case BOARD_ID_IOCTL_WRITE_HEADSET_ID: 
		case BOARD_ID_IOCTL_WRITE_MICPHONE_ID: 
		case BOARD_ID_IOCTL_WRITE_SPEAKER_ID: 
		case BOARD_ID_IOCTL_WRITE_VIBRATOR_ID: 
		case BOARD_ID_IOCTL_WRITE_TV_ID: 
		case BOARD_ID_IOCTL_WRITE_ECHIP_ID:
		case BOARD_ID_IOCTL_WRITE_HUB_ID:
		case BOARD_ID_IOCTL_WRITE_PMIC_ID: 
		case BOARD_ID_IOCTL_WRITE_REGULATOR_ID: 
		case BOARD_ID_IOCTL_WRITE_RTC_ID: 
		case BOARD_ID_IOCTL_WRITE_CAMERA_FRONT_ID: 
		case BOARD_ID_IOCTL_WRITE_CAMERA_BACK_ID: 	
		case BOARD_ID_IOCTL_WRITE_SENSOR_ANGLE_ID: 
		case BOARD_ID_IOCTL_WRITE_SENSOR_ACCEL_ID: 
		case BOARD_ID_IOCTL_WRITE_SENSOR_COMPASS_ID: 
		case BOARD_ID_IOCTL_WRITE_SENSOR_GYRO_ID: 
		case BOARD_ID_IOCTL_WRITE_SENSOR_LIGHT_ID: 
		case BOARD_ID_IOCTL_WRITE_SENSOR_PROXIMITY_ID:
		case BOARD_ID_IOCTL_WRITE_SENSOR_TEMPERATURE_ID:
		case BOARD_ID_IOCTL_READ_DEVICE_NAME_BY_ID:
			if(copy_from_user(&device_selected_temp, argp, sizeof(struct device_id_name)))
			{
				printk("%s:line=%d:fail to copy device_id_name from user\n",__func__, __LINE__);	
				mutex_unlock(&board_id->operation_mutex);	
				return -EFAULT;
			}
			DBG_ID("%s:write: type=%d,id=%d,name=%s\n",__func__, (int)device_selected_temp.type, (int)device_selected_temp.id, device_selected_temp.dev_name);
			break;
			
	}	

	switch(cmd)
	{	
		case BOARD_ID_IOCTL_READ_TP_ID:
		case BOARD_ID_IOCTL_READ_LCD_ID:
		case BOARD_ID_IOCTL_READ_KEY_ID:
		case BOARD_ID_IOCTL_READ_CODEC_ID:
		case BOARD_ID_IOCTL_READ_WIFI_ID:
		case BOARD_ID_IOCTL_READ_BT_ID:
		case BOARD_ID_IOCTL_READ_GPS_ID:
		case BOARD_ID_IOCTL_READ_FM_ID:
		case BOARD_ID_IOCTL_READ_MODEM_ID:
		case BOARD_ID_IOCTL_READ_DDR_ID:
		case BOARD_ID_IOCTL_READ_FLASH_ID:
		case BOARD_ID_IOCTL_READ_HDMI_ID:
		case BOARD_ID_IOCTL_READ_BATTERY_ID:
		case BOARD_ID_IOCTL_READ_CHARGE_ID:
		case BOARD_ID_IOCTL_READ_BACKLIGHT_ID:
		case BOARD_ID_IOCTL_READ_HEADSET_ID:
		case BOARD_ID_IOCTL_READ_MICPHONE_ID:
		case BOARD_ID_IOCTL_READ_SPEAKER_ID:
		case BOARD_ID_IOCTL_READ_VIBRATOR_ID:	
		case BOARD_ID_IOCTL_READ_TV_ID:
		case BOARD_ID_IOCTL_READ_ECHIP_ID:
		case BOARD_ID_IOCTL_READ_HUB_ID:	
		case BOARD_ID_IOCTL_READ_PMIC_ID:
		case BOARD_ID_IOCTL_READ_REGULATOR_ID:	
		case BOARD_ID_IOCTL_READ_RTC_ID:
		case BOARD_ID_IOCTL_READ_CAMERA_FRONT_ID:	
		case BOARD_ID_IOCTL_READ_CAMERA_BACK_ID:		
		case BOARD_ID_IOCTL_READ_SENSOR_ANGLE_ID:	
		case BOARD_ID_IOCTL_READ_SENSOR_ACCEL_ID:	
		case BOARD_ID_IOCTL_READ_SENSOR_COMPASS_ID:	
		case BOARD_ID_IOCTL_READ_SENSOR_GYRO_ID :	
		case BOARD_ID_IOCTL_READ_SENSOR_LIGHT_ID:	
		case BOARD_ID_IOCTL_READ_SENSOR_PROXIMITY_ID:
		case BOARD_ID_IOCTL_READ_SENSOR_TEMPERATURE_ID: 			
		case BOARD_ID_IOCTL_READ_SENSOR_PRESSURE_ID:	
			if(copy_to_user(argp, &device_selected_temp, sizeof(struct device_id_name)))
			{
				printk("%s:line=%d:fail to copy device_selected_temp.dev_name=%s to user\n",__func__,__LINE__,device_selected_temp.dev_name);	
				mutex_unlock(&board_id->operation_mutex);	
				return -EFAULT;
			}
			DBG_ID("%s:read: type=%d,id=%d,name=%s\n",__func__, (int)device_selected_temp.type, (int)device_selected_temp.id, device_selected_temp.dev_name);
			break;
			


		case BOARD_ID_IOCTL_WRITE_TP_ID:
			board_id->device_selected[DEVICE_TYPE_TP] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_LCD_ID:
			board_id->device_selected[DEVICE_TYPE_LCD] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_KEY_ID:
			board_id->device_selected[DEVICE_TYPE_KEY] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_CODEC_ID:
			board_id->device_selected[DEVICE_TYPE_CODEC] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_WIFI_ID:
			board_id->device_selected[DEVICE_TYPE_WIFI] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_BT_ID:
			board_id->device_selected[DEVICE_TYPE_BT] = device_selected_temp;
			break; 	
		case BOARD_ID_IOCTL_WRITE_GPS_ID:
			board_id->device_selected[DEVICE_TYPE_GPS] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_FM_ID:
			board_id->device_selected[DEVICE_TYPE_FM] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_MODEM_ID:
			board_id->device_selected[DEVICE_TYPE_MODEM] = device_selected_temp;
			break; 	
		case BOARD_ID_IOCTL_WRITE_DDR_ID:
			board_id->device_selected[DEVICE_TYPE_DDR] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_FLASH_ID:
			board_id->device_selected[DEVICE_TYPE_FLASH] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_HDMI_ID:
			board_id->device_selected[DEVICE_TYPE_HDMI] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_BATTERY_ID:
			board_id->device_selected[DEVICE_TYPE_BATTERY] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_CHARGE_ID:
			board_id->device_selected[DEVICE_TYPE_CHARGE] = device_selected_temp;
			break;  
		case BOARD_ID_IOCTL_WRITE_BACKLIGHT_ID:
			board_id->device_selected[DEVICE_TYPE_BACKLIGHT] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_HEADSET_ID:
			board_id->device_selected[DEVICE_TYPE_HEADSET] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_MICPHONE_ID:
			board_id->device_selected[DEVICE_TYPE_MICPHONE] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_SPEAKER_ID:
			board_id->device_selected[DEVICE_TYPE_SPEAKER] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_VIBRATOR_ID:
			board_id->device_selected[DEVICE_TYPE_VIBRATOR] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_TV_ID:
			board_id->device_selected[DEVICE_TYPE_TV] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_ECHIP_ID:
			board_id->device_selected[DEVICE_TYPE_ECHIP] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_HUB_ID:
			board_id->device_selected[DEVICE_TYPE_HUB] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_TPAD_ID:
			board_id->device_selected[DEVICE_TYPE_TPAD] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_PMIC_ID:
			board_id->device_selected[DEVICE_TYPE_PMIC] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_REGULATOR_ID:
			board_id->device_selected[DEVICE_TYPE_REGULATOR] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_RTC_ID:
			board_id->device_selected[DEVICE_TYPE_RTC] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_CAMERA_FRONT_ID:
			board_id->device_selected[DEVICE_TYPE_CAMERA_FRONT] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_CAMERA_BACK_ID:
			board_id->device_selected[DEVICE_TYPE_CAMERA_BACK] = device_selected_temp;
			break; 	
		case BOARD_ID_IOCTL_WRITE_SENSOR_ANGLE_ID:
			board_id->device_selected[DEVICE_TYPE_ANGLE] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_SENSOR_ACCEL_ID:
			board_id->device_selected[DEVICE_TYPE_ACCEL] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_SENSOR_COMPASS_ID:
			board_id->device_selected[DEVICE_TYPE_COMPASS] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_SENSOR_GYRO_ID:
			board_id->device_selected[DEVICE_TYPE_GYRO] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_SENSOR_LIGHT_ID:
			board_id->device_selected[DEVICE_TYPE_LIGHT] = device_selected_temp;
			break; 
		case BOARD_ID_IOCTL_WRITE_SENSOR_PROXIMITY_ID:
			board_id->device_selected[DEVICE_TYPE_PROXIMITY] = device_selected_temp;
			break;
		case BOARD_ID_IOCTL_WRITE_SENSOR_TEMPERATURE_ID:
			board_id->device_selected[DEVICE_TYPE_TEMPERATURE] = device_selected_temp;
			break;
			
	}

	
	mutex_unlock(&board_id->operation_mutex);

	switch(cmd)
	{
		case BOARD_ID_IOCTL_READ_DEVICE_NAME_BY_ID:		
			mutex_lock(&board_id->operation_mutex); 
			if(&board_id->device_start_addr[(unsigned)device_selected_temp.type][(unsigned)device_selected_temp.id])
			memcpy(&device_selected_last_temp, &board_id->device_start_addr[(unsigned)device_selected_temp.type][(unsigned)device_selected_temp.id], sizeof(struct device_id_name));
			else
			{
				printk("%s:fail to find device,type=%d,id=%d\n",__func__, device_selected_temp.type, device_selected_temp.id);
				mutex_unlock(&board_id->operation_mutex);
				return -1;
			}
			if(copy_to_user(argp, &device_selected_last_temp, sizeof(struct device_id_name)))
			{
				printk("%s:line=%d:fail to copy device_selected_last_temp.dev_name=%s to user\n",__func__,__LINE__,device_selected_last_temp.dev_name);	
				mutex_unlock(&board_id->operation_mutex);	
				return -EFAULT;
			}
			DBG_ID("%s:read: type=%d,id=%d,name=%s\n",__func__, (int)device_selected_last_temp.type, (int)device_selected_last_temp.id, device_selected_last_temp.dev_name);
			
			mutex_unlock(&board_id->operation_mutex);	
			break;

		case BOARD_ID_IOCTL_READ_AREA_NAME_BY_ID:
			mutex_lock(&board_id->operation_mutex);	
			if(copy_from_user(&language_last_select, argp, sizeof(struct area_id_name)))
			{
				printk("%s:line=%d:fail to copy area_id_name from user\n",__func__, __LINE__);	
				mutex_unlock(&board_id->operation_mutex);	
				return -EFAULT;
			}			
			
			if(copy_to_user(argp, &board_id->area_area_id_name[language_last_select.id], sizeof(struct area_id_name)))
			{
				printk("%s:fail to copy area_id_name to user\n",__func__);
				mutex_unlock(&board_id->operation_mutex);
				return -EFAULT;
			}
			
			mutex_unlock(&board_id->operation_mutex);
			
			DBG_ID("%s:BOARD_ID_IOCTL_READ_LANGUAGE_NAME_BY_ID:%s_%s\n",__func__,language_last_select.locale_language,language_last_select.locale_region);
			break;	

		case BOARD_ID_IOCTL_READ_OPERATOR_NAME_BY_ID:
			mutex_lock(&board_id->operation_mutex);	
			if(copy_from_user(&operator_last_select, argp, sizeof(struct operator_id_name)))
			{
				printk("%s:line=%d:fail to copy operator_id_name from user\n",__func__, __LINE__);	
				mutex_unlock(&board_id->operation_mutex);	
				return -EFAULT;
			}	

			for(i=OPERATOR_ID_20000_NO_OPERATOR; i<OPERATOR_ID_NUMS; i++)
			{
				if(operator_last_select.id == board_id->area_operator_id_name[i].id)
				{
					if(copy_to_user(argp, &board_id->area_operator_id_name[i], sizeof(struct operator_id_name)))
					{
						printk("%s:fail to copy operator_id_name to user\n",__func__);
						mutex_unlock(&board_id->operation_mutex);
						return -EFAULT;
					}
				}
			}
			mutex_unlock(&board_id->operation_mutex);
			
			DBG_ID("%s:BOARD_ID_IOCTL_READ_OPERATOR_NAME_BY_ID:%s_%s\n",__func__,operator_last_select.operator_name,operator_last_select.locale_region);
			break;	

		case BOARD_ID_IOCTL_READ_RESERVE_NAME_BY_ID:
			mutex_lock(&board_id->operation_mutex);	
			if(copy_from_user(&reserve_last_select, argp, sizeof(struct reserve_id_name)))
			{
				printk("%s:line=%d:fail to copy reserve_id_name from user\n",__func__, __LINE__);	
				mutex_unlock(&board_id->operation_mutex);	
				return -EFAULT;
			}			
			
			if(copy_to_user(argp, &board_id->area_reserve_id_name[reserve_last_select.id], sizeof(struct reserve_id_name)))
			{
				printk("%s:fail to copy reserve_id_name to user\n",__func__);
				mutex_unlock(&board_id->operation_mutex);
				return -EFAULT;
			}
			
			mutex_unlock(&board_id->operation_mutex);
			
			DBG_ID("%s:BOARD_ID_IOCTL_READ_RESERVE_NAME_BY_ID:%s_%s\n",__func__,reserve_last_select.reserve_name, reserve_last_select.locale_region);
			break;	
				
	}

	DBG_ID("%s:cmd=%d\n",__func__,cmd);
	return 0;
}

static int __init board_id_init(void)
{
	int result ;	
	struct proc_dir_entry *board_id_proc_entry;
	
	struct board_id_private_data *board_id = g_board_id;

	if(!board_id)
		return -1;

	board_id->id_fops.owner = THIS_MODULE;
	board_id->id_fops.open = board_id_open;
	board_id->id_fops.release = board_id_release;	
	board_id->id_fops.unlocked_ioctl = board_id_ioctl;

	board_id->id_miscdev.minor = MISC_DYNAMIC_MINOR;
	board_id->id_miscdev.name = "board_id_misc";
	board_id->id_miscdev.fops = &board_id->id_fops;
	result = misc_register(&board_id->id_miscdev);
	if(result < 0) {
		printk("%s:misc_register err,ret=%d\n",__func__,result);
		return result;
	}

	
#if defined(CONFIG_BOARD_ID_AUTO_XML)
	board_id_proc_entry = proc_create("driver/board_id_dbg", 0660, NULL, &board_id_proc_fops); 
#endif
	board_id->board_id_data_filp = NULL;	
	board_id->board_id_area_filp = NULL;	
	board_id->board_id_device_filp = NULL;

	printk("%s:\n",__func__);

	return 0;
	
}

static void __exit board_id_exit(void)
{
	struct board_id_private_data *board_id = g_board_id;

	if(!board_id)
		return ;

	misc_deregister(&board_id->id_miscdev);

	set_fs(board_id->board_id_data_fs);
  	filp_close(board_id->board_id_data_filp, NULL);

	set_fs(board_id->board_id_area_fs);
  	filp_close(board_id->board_id_area_filp, NULL);

	set_fs(board_id->board_id_device_fs);
  	filp_close(board_id->board_id_device_filp, NULL);
}

subsys_initcall_sync(board_id_init);
module_exit(board_id_exit);

MODULE_AUTHOR("ROCKCHIP Corporation:lw@rock-chips.com");
MODULE_DESCRIPTION("rockchip board id misc interface for vendor");
MODULE_LICENSE("GPL");

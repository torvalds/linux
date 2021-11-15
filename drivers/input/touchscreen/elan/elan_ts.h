// SPDX-License-Identifier: GPL-2.0
/*
 * ELAN HID-I2C TouchScreen driver.
 *
 * Copyright (C) 2014 Elan Microelectronics Corporation.
 *
 * Author: Chuming Zhang <chuming.zhang@elanic.com.cn>
 */

#ifndef _LINUX_ELAN_TS_H
#define _LINUX_ELAN_TS_H

#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/regulator/consumer.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
    #include <linux/pm.h>
    #include <linux/earlysuspend.h>
#endif

#if defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#endif
#include <linux/poll.h>
#include <linux/wait.h>
/*fw upgrade fuction switch*/
//#define IAP_PORTION

/*define elan device name*/
#define ELAN_TS_NAME "elan_ts"

#define MIN(x,y) ((((x) - (y)) < 0) ? (x) : (y))
#define FLASH_PAGE_PER_TIMES	(30)
#define FW_PAGE_SIZE			(132)
#define IAP_CMD_HEADER_LEN		(9)
#define IAP_FLASH_SIZE  (FLASH_PAGE_PER_TIMES * FW_PAGE_SIZE)

#define ELAN_VTG_MIN_UV		3300000//2850000
#define ELAN_VTG_MAX_UV		3300000//2850000
#define ELAN_I2C_VTG_MIN_UV	1800000
#define ELAN_I2C_VTG_MAX_UV	1800000

#define HID_REPORT_MAX_LEN		(67*2)
#define NORMAL_REPORT_MAX_LEN	(55)
#define HID_CMD_LEN				(37)
#define HID_RECV_LEN			(67)


#define RETRY_TIMES		(3)

#define FINGERS_NUM 10

struct ts_chip_hw_info {
	int intr_gpio;
	int rst_gpio;
	int irq_num;
	uint16_t screen_x;
	uint16_t screen_y;
};

struct elan_fw_info {
	int fw_ver;
	int fw_id;
	int fw_bcv;
	int fw_bcl;
	int tx; /*tp module y<->tx*/
	int rx; /*tp module x<->rx*/
	int finger_xres;
	int finger_yres;
	int pen_xres;
	int pen_yres;
	int finger_osr;
	int testsolversion;
	int testversion;
	int solutionversion;
	int whck_ver;
};

struct elan_update_fw_info {
	char *FwName;
	char fw_local_path[50];
	const u8 *FwData;
	const struct firmware *p_fw_entry;
	int PageNum;
	int FwSize;
	int PageSize; 
	u16 remark_id;
};

struct elan_i2c_operation{
	int (*send)(const uint8_t *, int len);
	int (*recv)(uint8_t *, int len);
	int (*poll)(void);
};

struct elan_packet_struct {
	int finger_num;
	int finger_id;
	int pen_id;
	int vaild_size;
};


struct elan_finger_struct {
	int fid;				/*finger identify*/
	int fsupport_num;		/*support finger num*/
	int fbuf_valid_size;	/*hid: 67/67*2, normal:8/18/35/55*/
	int fvalid_num;			/*current buf support finger num*/
	int fbits;				/*current finger is contact or not*/
	int fbutton_value;		/*mutual button*/
	int freport_idx;		/*parse coordinate start byte*/
	int fshift_byte;		/*parse shift byte*/
};

struct elan_stylus_struct {
	int pid;				/*stylus identify*/
	int pbuf_valid_size;	/*stylus buf size*/
	int pbutton_value;		/*mutual button,may not use*/
	int preport_idx;		/*parse coordinate start byte*/
	int shitf_byte;			/*may not use*/
//	int tip_status;			/**/
	int tip_status;			/*contact status*/
	int inrange_status;		/*hover status*/
	int key;					/*key status*/
	int	eraser;				/*eraser*/
	int inver;				/*inver*/
	int	barrel;				/*barrel*/
	int barrel_tip;			/*barrel+tip*/
};

struct elan_report_struct {
	struct elan_finger_struct finger;
	struct elan_stylus_struct stylus;
//	uint8_t *buf;			/*elan report recv buf*/
//	int finger_id;			/*finger identify*/
//	int pen_id;				/*stylu identify*/
//	int buf_valid_size;		/*hid: 67/67*2, normal:8/18/35/55*/
//	int finger_num;			/*support finger num*/
//	int valid_finger_num;	/*current buf support finger num*/
//	int button_byte;		/*muxtual button*/
//	int report_idx;			/*coordinate calculate start index*/
//	int shift_byte;			/*calculate shift*/
//	int fbits;				/*current finger/pen index is valid or not*/
	int tool_type;			/**/
};


struct elan_ts_data {
	struct ts_chip_hw_info hw_info; /*elan touch need gpio*/
	struct i2c_client *client;
	struct elan_fw_info fw_info;
	struct elan_update_fw_info update_info;
	struct elan_i2c_operation *ops;
	struct elan_report_struct report;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;	/*for early suspend*/
#endif

	struct regulator *vdd;			/*tp vdd*/
	struct regulator *vcc_i2c;		/*tp vio*/
	bool power_enabled;				/*power setting flag*/

	int power_lock;					/*for i2ctransfer on power off flage*/
	int recover;					/*fw incomplete flag*/
	struct miscdevice firmware;		/*for misc dev*/
	struct proc_dir_entry *p;		/*creat proc dev*/

	struct input_dev *finger_idev;	/*for register finger input device*/
	struct input_dev *pen_idev;		/*for register pen inpur device*/

#if defined(CONFIG_FB)
	struct notifier_block fb_notif;	/*FB callback*/
#endif
	
	int chip_type;					/*chip protocol */
	int report_type;				/*report protocol*/
	int fw_store_type;				/*fw stroe*/


	struct workqueue_struct *init_elan_ic_wq; /*for ic initial*/
	struct delayed_work init_work;

	struct workqueue_struct *elan_wq;	/*for irq handler*/
	struct work_struct ts_work;
	uint8_t level;

	int int_val;					/*for user space*/
	int	user_handle_irq;			/*for switch user or kernel handle irq*/
	wait_queue_head_t elan_userqueue; /*wait queue for user space read data*/
	int irq_lock_flag;				/*irq enable/disable flage, 1=>irq has been disable, 0=>has been enable*/
	struct mutex irq_mutex;			/* Guards against concurrent access to the irq_lock_flag*/	
};


struct vendor_map
{
	int vendor_id;		//lcm ID
	char vendor_name[30]; //lcm vendor name
	uint8_t* fw_array;  // lcm_vendor_fw
	int fw_size;
	int fw_id;
};



enum elan_get_vendor_fw_type {
	FROM_SYS_ETC_FIRMWARE,
	FROM_SDCARD_FIRMWARE,
	FROM_DRIVER_FIRMWARE,
};

enum elan_report_protocol_type {
	PROTOCOL_TYPE_A	= 0x00,
	PROTOCOL_TYPE_B	= 0x01,
};

enum elan_chip_protocol_type {
	HID_TYPE_PROTOCOL		= 0x01,
	NORMAL_TYPE_PROTOCOL	= 0x00,
};

enum elan_fw_status {
	HID_FW_NORMAL_MODE		= 0x20,
	HID_FW_RECOVERY_MODE	= 0x56,
	NORMAL_FW_NORMAL_MODE	= 0x55,
	NORMAL_FW_RECOVERY_MODE	= 0x80,
};

enum fw_update_type {
	FORCED_UPGRADE	= 0x01,
	COMPARE_UPGRADE	= 0x02,
	UNKNOW_TYPE		= -1,
};

enum packet_head_type {
	CMD_S_PKT	= 0x52,
	CMD_R_PKT	= 0x53,
	CMD_W_PKT	= 0x54,
	REG_R_PKT	= 0x5B,
	REG_S_PKT	= 0x9B,
};


enum report_packet_size {
	NOR2_SIZE	=	8,
	NOR5_SIZE	=	18,
	NOR10_SIZE	=	35,
	HID5_SIZE	=	67,
	HID10_SIZE	=	67*2,
};

enum report_packet_id {
	NOR2_FID	=	0x5A,
	NOR5_FID	=	0x5D,
	NOR10_FID	=	0x62,
	HID_FID		=	0x01,
	HID_PID		=	0x07,
};

enum report_tool_type {
	ELAN_FINGER	=	0x01,
	ELAN_PEN	=	0x02,
};

enum elan_power_status {
	PWR_STATE_DEEP_SLEEP = 0x00,
	PWR_STATE_NORMAL	 = 0x01,
};





/*************************dev file macro switch********************/
#define ELAN_IAP_DEV

#ifdef ELAN_IAP_DEV
#define ELAN_IOCTLID	0xD0
#define IOCTL_I2C_SLAVE	_IOW(ELAN_IOCTLID,  1, int)
#define IOCTL_MAJOR_FW_VER  _IOR(ELAN_IOCTLID, 2, int)
#define IOCTL_MINOR_FW_VER  _IOR(ELAN_IOCTLID, 3, int)
#define IOCTL_RESET  _IOR(ELAN_IOCTLID, 4, int)
#define IOCTL_IAP_MODE_LOCK  _IOR(ELAN_IOCTLID, 5, int)
#define IOCTL_CHECK_RECOVERY_MODE  _IOR(ELAN_IOCTLID, 6, int)
#define IOCTL_FW_VER  _IOR(ELAN_IOCTLID, 7, int)
#define IOCTL_X_RESOLUTION  _IOR(ELAN_IOCTLID, 8, int)
#define IOCTL_Y_RESOLUTION  _IOR(ELAN_IOCTLID, 9, int)
#define IOCTL_FW_ID  _IOR(ELAN_IOCTLID, 10, int)
#define IOCTL_ROUGH_CALIBRATE  _IOR(ELAN_IOCTLID, 11, int)
#define IOCTL_IAP_MODE_UNLOCK  _IOR(ELAN_IOCTLID, 12, int)
#define IOCTL_I2C_INT  _IOR(ELAN_IOCTLID, 13, int)
#define IOCTL_RESUME  _IOR(ELAN_IOCTLID, 14, int)
#define IOCTL_POWER_LOCK  _IOR(ELAN_IOCTLID, 15, int)
#define IOCTL_POWER_UNLOCK  _IOR(ELAN_IOCTLID, 16, int)
#define IOCTL_FW_UPDATE  _IOR(ELAN_IOCTLID, 17, int)
#define IOCTL_BC_VER  _IOR(ELAN_IOCTLID, 18, int)
#define IOCTL_2WIREICE  _IOR(ELAN_IOCTLID, 19, int)
#define IOCTL_VIAROM	_IOR(ELAN_IOCTLID, 20, int) 
#define IOCTL_VIAROM_CHECKSUM	_IOW(ELAN_IOCTLID, 21, unsigned long)
#define IOCTL_GET_UPDATE_PROGREE	_IOR(CUSTOMER_IOCTLID,  2, int)
#define IOCTL_USER_HANDLE_IRQ	_IOR(ELAN_IOCTLID, 22, int)
#define IOCTL_KERN_HANDLE_IRQ	_IOR(ELAN_IOCTLID, 23, int)
#endif


//define print log
//#define LOG_TAG(tag) "[tag]: %s() line: %d "
#define TP_DEBUG	0//"dbg"
#define TP_WARNING	1
#define TP_INFO		2//"info"
#define TP_ERR		4//"err"

#define print_log(level, fmt, args...) \
do { \
		if (level > TP_WARNING) \
			printk("[elan]:"fmt"\n",##args);\
} while(0)


//extern function
extern void elan_check_update_flage(struct elan_ts_data *ts);
extern int elan__fw_packet_handler(struct i2c_client *client);
extern int elan__hello_packet_handler(struct i2c_client *client, int chip_type);
extern void elan_ts_hw_reset(struct ts_chip_hw_info *hw_info);
extern void elan_switch_irq(struct elan_ts_data *ts, int on);
extern int elan_ts_calibrate(struct i2c_client *client);
extern int elan_FW_Update(struct i2c_client *client);
extern int elan_ic_status(struct i2c_client *client);
extern int elan_ts_check_calibrate(struct i2c_client *client);
extern int elan_sysfs_attri_file(struct elan_ts_data *ts);
extern void elan_sysfs_attri_file_remove(struct elan_ts_data *ts);
extern int elan_get_vendor_fw(struct elan_ts_data *ts, int type);
extern int elan_tp_module_test(struct elan_ts_data *ts);
#endif /* _LINUX_ELAN_KTF_H */

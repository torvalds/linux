/*
 *  Atmel maXTouch Touchscreen Controller Driver
 *
 *  
 *  Copyright (C) 2010 Atmel Corporation
 *  Copyright (C) 2009 Raphael Derosso Pereira <raphaelpereira@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * 
 * Driver for Atmel maXTouch family of touch controllers.
 *
 */

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/debugfs.h>
#include <linux/cdev.h>
#include <linux/mutex.h>

#include <asm/uaccess.h>

#include <linux/workqueue.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <asm/io.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <mach/board.h>
#include "atmel_mxt1386.h"

//#define YANSEN_DEBUG	//yansen 20101230 regulator is under control in kernel (sbl too)
#ifdef YANSEN_DEBUG
#include <linux/regulator/consumer.h>
#endif

#define DRIVER_VERSION "0.9a"
#define CONFIG_ABS_MT_TRACKING_ID

static int debug = 0;
static int comms = 1;
static int ts_irq = 0;
static u32 last_key=0;

module_param(debug, int, 0644);
module_param(comms, int, 0644);

MODULE_PARM_DESC(debug, "Activate debugging output");
MODULE_PARM_DESC(comms, "Select communications mode");

static int mxt_read_block(struct i2c_client *client, u16 addr, u16 length,
			  u8 *value);
static int mxt_write_byte(struct i2c_client *client, u16 addr, u8 value);
static int mxt_write_block(struct i2c_client *client, u16 addr, u16 length,
			   u8 *value);

/* Device Info descriptor */
/* Parsed from maXTouch "Id information" inside device */
struct mxt_device_info {
	u8   family_id;
	u8   variant_id;
	u8   major;
	u8   minor;
	u8   build;
	u8   num_objs;
	u8   x_size;
	u8   y_size;
	char family_name[16];	 /* Family name */
	char variant_name[16];    /* Variant name */
	u16  num_nodes;           /* Number of sensor nodes */
};

/* object descriptor table, parsed from maXTouch "object table" */
struct mxt_object {
	u16 chip_addr;
	u8  type;
	u8  size;
	u8  instances;
	u8  num_report_ids;
};


/* Mapping from report id to object type and instance */
struct report_id_map {
	u8  object;
	u8  instance;
/*
 * This is the first report ID belonging to object. It enables us to
 * find out easily the touch number: each touch has different report
 * ID (which are assigned to touches in increasing order). By
 * subtracting the first report ID from current, we get the touch
 * number.
 */
	u8  first_rid;
};

/* Driver datastructure */
struct mxt_data {
	struct i2c_client    *client;
	struct input_dev     *input;
	char                 phys_name[32];
	int                  irq;

	u16                  last_read_addr;
	bool                 new_msgs;
	u8                   *last_message;

	int                  valid_irq_counter;
	int                  invalid_irq_counter;
	int                  irq_counter;
	int                  message_counter;
	int                  read_fail_counter;

	int                  bytes_to_read;

	struct delayed_work  dwork;
	struct work_struct   work;
	struct workqueue_struct *queue;
	
	u8                   xpos_format;
	u8                   ypos_format;

	u8                   numtouch;

	struct mxt_device_info	device_info;

	u32		     info_block_crc;
	u32                  configuration_crc;
	u16                  report_id_count;
	struct report_id_map *rid_map;
	struct mxt_object    *object_table;

	u16                  msg_proc_addr;
	u8                   message_size;

	u16                  max_x_val;
	u16                  max_y_val;

	int                 (*init_hw)(struct device *dev);
	void                 (*exit_hw)(struct device *dev);
	u8                   (*valid_interrupt)(void);
	u8                   (*read_chg)(void);

	/* debugfs variables */
	struct dentry        *debug_dir;
	int                  current_debug_datap;

	struct mutex         debug_mutex;
	u16                  *debug_data;

        /* Character device variables */
	struct cdev          cdev;
	struct cdev          cdev_messages;  /* 2nd Char dev for messages */
	dev_t                dev_num;
	struct class         *mxt_class;

	u16                  address_pointer;
	bool                 valid_ap;

	/* Message buffer & pointers */
	char                 *messages;
	int                  msg_buffer_startp, msg_buffer_endp;
        /* Put only non-touch messages to buffer if this is set */
	char                 nontouch_msg_only; 
	struct mutex         msg_mutex;
};

static struct mxt_data          *atmel_mxt;


#define I2C_RETRY_COUNT 5
#define I2C_PAYLOAD_SIZE 254

/* Returns the start address of object in mXT memory. */
#define	MXT_BASE_ADDR(object_type, mxt)					\
	get_object_address(object_type, 0, mxt->object_table,           \
			   mxt->device_info.num_objs)

/* Maps a report ID to an object type (object type number). */
#define	REPORT_ID_TO_OBJECT(rid, mxt)			\
	(((rid) == 0xff) ? 0 : mxt->rid_map[rid].object)

/* Maps a report ID to an object type (string). */
#define	REPORT_ID_TO_OBJECT_NAME(rid, mxt)			\
	object_type_name[REPORT_ID_TO_OBJECT(rid, mxt)]

/* Returns non-zero if given object is a touch object */
#define IS_TOUCH_OBJECT(object) \
	((object == MXT_TOUCH_MULTITOUCHSCREEN_T9) || \
	 (object == MXT_TOUCH_KEYARRAY_T15) ||	\
	 (object == MXT_TOUCH_PROXIMITY_T23) || \
	 (object == MXT_TOUCH_SINGLETOUCHSCREEN_T10) || \
	 (object == MXT_TOUCH_XSLIDER_T11) || \
	 (object == MXT_TOUCH_YSLIDER_T12) || \
	 (object == MXT_TOUCH_XWHEEL_T13) || \
	 (object == MXT_TOUCH_YWHEEL_T14) || \
	 (object == MXT_TOUCH_KEYSET_T31) || \
	 (object == MXT_TOUCH_XSLIDERSET_T32) ? 1 : 0)

#define print_ts(level, ...) \
	do { \
		if (debug >= (level)) \
			printk(__VA_ARGS__); \
	} while (0) 

/* following are debug infomation switch, add by acgzx */
#define TOUCH_RESET_PIN				RK29_PIN6_PC3
#define TOUCH_INT_PIN				RK29_PIN0_PA2
#define LCDC_STANDBY_PIN			RK29_PIN6_PD1
//#define TS_PEN_IRQ_GPIO RK29_PIN0_PA2//61
		
#define WORKQ_RIGHTNOW		1
#define TOUCH_KEY_EN	1

/* macro switch for debug log */		 
#define MXT1386_DEBUG_LOG_EN	0// 1		
#if	MXT1386_DEBUG_LOG_EN
#define MXT1386_LOG 	printk
#else
#define MXT1386_LOG
#endif

#define WRITE_MT_CONFIG		1//0

struct mxt_key_info{
    u32  start;
    u32  end;
    u32 virtual_x;
    u32 virtual_y;
    u32  code;
};

const struct mxt_key_info key_info[] = {
	{980, 1280, 3100, 4200, KEY_SEARCH},
    {1600, 1900, 3000, 4200, KEY_HOME},
    {2200, 2500, 1100, 4200, KEY_MENU},
    {2750, 3100, 1000, 4200, KEY_BACK},
};

/* 
 * Check whether we have multi-touch enabled kernel; if not, report just the
 * first touch (on mXT224, the maximum is 10 simultaneous touches).
 * Because just the 1st one is reported, it might seem that the screen is not
 * responding to touch if the first touch is removed while the screen is being
 * touched by another finger, so beware. 
 *
 * TODO: investigate if there is any standard set of input events that uppper
 * layers are expecting from a touchscreen? These can however be different for
 * different platforms, and customers may have different opinions too about
 * what should be interpreted as right-click, for example. 
 *
 */
#ifdef EFFICIENT_REPORT
static void report_sync(struct mxt_data *mxt)
{
	input_sync(mxt->input);
}

static inline void report_mt(int touch_number, int size, int x, int y, struct
			mxt_data *mxt) {
	input_report_abs(mxt->input, ABS_MT_TRACKING_ID, touch_number);

	if(size != 0)
	{
		input_report_abs(mxt->input, ABS_MT_POSITION_X, x);
		input_report_abs(mxt->input, ABS_MT_POSITION_Y, y);
		input_report_abs(mxt->input, ABS_MT_TOUCH_MAJOR, 10);
		input_report_abs(mxt->input, ABS_MT_WIDTH_MAJOR, 20);
	}
	else
	{
		input_report_abs(mxt->input, ABS_MT_TOUCH_MAJOR, 0);
		input_report_abs(mxt->input, ABS_MT_WIDTH_MAJOR, 0);
	}
	input_mt_sync(mxt->input);
}
#else
#define MAX_FINGERS		5
struct finger {
	int x;
	int y;
	int size;
};

static struct finger fingers[MAX_FINGERS] = {};

static void report_mt(int touch_number, int size, int x, int y, struct
		mxt_data *mxt) 
{
	MXT1386_LOG(KERN_INFO "report_mt touch_number=%d, size=%d, x=%d, t=%d\n", __func__, 
			touch_number, size, x, y);
	if (touch_number > MAX_FINGERS ||  touch_number < 0) {
		return;
	}
	fingers[touch_number].x = x;
	fingers[touch_number].y = y;
	fingers[touch_number].size = size;
	MXT1386_LOG(KERN_INFO "report_mt() OUT\n", __func__);
}

static void report_sync(struct mxt_data *mxt)
{
	int i;
	int fin = 0;

	MXT1386_LOG(KERN_INFO "report_sync() IN\n", __func__);

	for (i = 0; i < MAX_FINGERS; i++) {
		if (fingers[i].size != 0) {
			fin++;
			MXT1386_LOG(KERN_INFO "report_sync touch_number=%d, x=%d, y=%d, width=%d\n",
					i, fingers[i].x, fingers[i].y, fingers[i].size);
			input_report_abs(mxt->input, ABS_MT_POSITION_X, fingers[i].x);
			input_report_abs(mxt->input, ABS_MT_POSITION_Y, fingers[i].y);
			input_report_abs(mxt->input, ABS_MT_TOUCH_MAJOR, 10);
			input_report_abs(mxt->input, ABS_MT_WIDTH_MAJOR, 20);
			input_mt_sync(mxt->input);
		}
	}

	if (fin == 0) {
		MXT1386_LOG(KERN_INFO "report_sync no fingler\n", __func__);
		input_report_abs(mxt->input, ABS_MT_TOUCH_MAJOR, 0);
		input_report_abs(mxt->input, ABS_MT_WIDTH_MAJOR, 0);
		input_mt_sync(mxt->input);
	}

	input_sync(mxt->input);
	MXT1386_LOG(KERN_INFO "report_sync() OUT\n", __func__);
}
#endif

static inline void report_gesture(int data, struct mxt_data *mxt)
{
	//input_event(mxt->input, EV_MSC, MSC_GESTURE, data); 
}

static const u8	*object_type_name[] = {
	[0]  = "Reserved",
	[5]  = "GEN_MESSAGEPROCESSOR_T5",
	[6]  = "GEN_COMMANDPROCESSOR_T6",
	[7]  = "GEN_POWERCONFIG_T7",
	[8]  = "GEN_ACQUIRECONFIG_T8",
	[9]  = "TOUCH_MULTITOUCHSCREEN_T9",
	[15] = "TOUCH_KEYARRAY_T15",
	[17] = "SPT_COMMSCONFIG_T18",
	[19] = "SPT_GPIOPWM_T19",
	[20] = "PROCI_GRIPFACESUPPRESSION_T20",
	[22] = "PROCG_NOISESUPPRESSION_T22",
	[23] = "TOUCH_PROXIMITY_T23",
	[24] = "PROCI_ONETOUCHGESTUREPROCESSOR_T24",
	[25] = "SPT_SELFTEST_T25",
	[27] = "PROCI_TWOTOUCHGESTUREPROCESSOR_T27",
	[28] = "SPT_CTECONFIG_T28",
	[37] = "DEBUG_DIAGNOSTICS_T37",
	[38] = "SPT_USER_DATA_T38",
	[40] = "PROCI_GRIPSUPPRESSION_T40",
	[41] = "PROCI_PALMSUPPRESSION_T41",
	[42] = "PROCI_FACESUPPRESSION_T42",
	[43] = "SPT_DIGITIZER_T43",
	[44] = "SPT_MESSAGECOUNT_T44",
};

static u16 get_object_address(uint8_t object_type,
			      uint8_t instance,
			      struct mxt_object *object_table,
			      int max_objs);

int mxt_write_ap(struct mxt_data *mxt, u16 ap);

static int mxt_read_block_wo_addr(struct i2c_client *client,
			   u16 length,
				u8 *value);

#if WRITE_MT_CONFIG
static const u8 atmel_1386_T7_config[] = {
140,  //IDLEACQINT
25, //ACTVACQINT
60, //ACTV2IDLETO
};

static const u8 atmel_1386_T9_config[] = {
0x03,  //CTRL 03
0,  //XORIGIN
0,  //YORIGIN
28,  //XSIZE
42,  //YSIZE
0,  //AKSCFG
32,  //BLEN
60,  //TCHTHR
3,  //TCHDI
7, //1,//7,  //ORIENT//xhh
0,  //MRGTIMEOUT
5,  //MOVHYSTI
5, //MOVHYSTN
18,  //MOVFILTER
10,  //NUMTOUCH
10,  //MRGHYST
10,  //MRGTHR
10,  //AMPHYST
0xff, //XRANGE low
0x0f, //XRANGE high
0xff, //YRANGE low
0x0f, //YRANGE high
0, //XLOCLIP
0, //XHICLIP
0, //YLOCLIP
0, //YHICLIP
0, //XEDGECTRL
0, //XEDGEDIST
0, //YEDGECTRL
0, //YEDGEDIST
0, //JUMPLIMIT
0, //TCHHYST
45, //XPITCH
46, //YPITCH
};

static int atmel_1386_write_T7(struct mxt_data *mxt)
{
	int ret, i;
	int size  = sizeof(atmel_1386_T7_config);
	for(i = 0; i<size; i++)
	{
		ret = mxt_write_byte(mxt->client,
			      MXT_BASE_ADDR(MXT_GEN_POWERCONFIG_T7, mxt) +
			      i,
			      atmel_1386_T7_config[i]);
		
		if (ret < 0) {
				MXT1386_LOG(KERN_INFO "%s, Error writing to atmel T7!\n", __func__);
		}
	}
	return ret;
}

static int atmel_1386_write_T9(struct mxt_data *mxt)
{
	int ret, i;
	int size  = sizeof(atmel_1386_T9_config);
	
	for(i = 0; i<size; i++)
	{
		ret = mxt_write_byte(mxt->client,
			      MXT_BASE_ADDR(MXT_TOUCH_MULTITOUCHSCREEN_T9, mxt) +
			      i,
			      atmel_1386_T9_config[i]);
		
		if (ret < 0) {
				MXT1386_LOG(KERN_INFO "%s, Error writing to atmel T9!\n", __func__);				
		}
	}
	return ret;
}

static int atmel_1386_write_config(struct mxt_data *mxt)
{
	atmel_1386_write_T7(mxt);
	//atmel_1386_write_T9(mxt);

	mxt_write_byte(mxt->client,
			      MXT_BASE_ADDR(MXT_GEN_COMMANDPROCESSOR_T6, mxt) +
			      MXT_ADR_T6_BACKUPNV,
			      MXT_CMD_T6_BACKUP);
	/*		      
	mxt_write_byte(mxt->client,
			      MXT_BASE_ADDR(MXT_GEN_COMMANDPROCESSOR_T6, mxt) +
			      MXT_ADR_T6_RESET,
			      1);
	*/
	return 0;
}
#endif

static u8 mxt_valid_interrupt_dummy(void)
{
	return 0;
}

static int mxt_buf_test(const u8 *src, u8 length)
{
	u8 i;
	u8 *pbuf = (u8*)src;

	MXT1386_LOG(KERN_INFO "%s, buf test start..., length = %d\n", __func__, length);
	for(i=0; i<length; i++) {
		MXT1386_LOG("0x%02x  ", pbuf[i]);
		if(0==(i+1)%10)
			MXT1386_LOG("\n");
	}
	MXT1386_LOG(KERN_INFO "%s, buf test end...\n", __func__);

	return 0;
}

ssize_t debug_data_read(struct mxt_data *mxt, char *buf, size_t count, 
			loff_t *ppos, u8 debug_command){
	int i;
	u16 *data;
	u16 diagnostics_reg;
	int offset = 0;
	int size;
	int read_size;
	int error;
	char *buf_start;
	u16 debug_data_addr;
	u16 page_address;
	u8 page;
	u8 debug_command_reg;

	data = mxt->debug_data;
	if (data == NULL)
		return -EIO;

	/* If first read after open, read all data to buffer. */
	if (mxt->current_debug_datap == 0){

		diagnostics_reg = MXT_BASE_ADDR(MXT_GEN_COMMANDPROCESSOR_T6, 
						mxt) + 
			          MXT_ADR_T6_DIAGNOSTIC;
		if (count > (mxt->device_info.num_nodes * 2))
			count = mxt->device_info.num_nodes;
	
		debug_data_addr = MXT_BASE_ADDR(MXT_DEBUG_DIAGNOSTIC_T37, mxt)+ 
			          MXT_ADR_T37_DATA;
		page_address = MXT_BASE_ADDR(MXT_DEBUG_DIAGNOSTIC_T37, mxt) +
			       MXT_ADR_T37_PAGE;
		error = mxt_read_block(mxt->client, page_address, 1, &page);
		if (error < 0)
			return error;
		MXT1386_LOG(KERN_INFO "debug data page = %d\n", __func__, page);		
		while (page != 0) {
			error = mxt_write_byte(mxt->client, 
					diagnostics_reg, 
					MXT_CMD_T6_PAGE_DOWN);
			if (error < 0)
				return error;
			/* Wait for command to be handled; when it has, the
			   register will be cleared. */
			debug_command_reg = 1;
			while (debug_command_reg != 0) {
				error = mxt_read_block(mxt->client, 
						diagnostics_reg, 1,
						&debug_command_reg);
				if (error < 0)
					return error;
				MXT1386_LOG(KERN_INFO "Waiting for debug diag command "
					"to propagate...\n", __func__);

			}
		        error = mxt_read_block(mxt->client, page_address, 1, 
					&page);
			if (error < 0)
				return error;
			MXT1386_LOG(KERN_INFO "debug data page = %d\n", __func__, page);	
		}

		/*
		 * Lock mutex to prevent writing some unwanted data to debug
		 * command register. User can still write through the char 
		 * device interface though. TODO: fix?
		 */

		mutex_lock(&mxt->debug_mutex);
		/* Configure Debug Diagnostics object to show deltas/refs */
		error = mxt_write_byte(mxt->client, diagnostics_reg,
				debug_command);

                /* Wait for command to be handled; when it has, the
		 * register will be cleared. */
		debug_command_reg = 1;
		while (debug_command_reg != 0) {
			error = mxt_read_block(mxt->client, 
					diagnostics_reg, 1,
					&debug_command_reg);
			if (error < 0)
				return error;
			MXT1386_LOG(KERN_INFO "Waiting for debug diag command "
				"to propagate...\n", __func__);

		}	

		if (error < 0) {
			MXT1386_LOG(KERN_INFO "Error writing to maXTouch device!\n", __func__);
			return error;
		}
	
		size = mxt->device_info.num_nodes * sizeof(u16);

		while (size > 0) {
			read_size = size > 128 ? 128 : size;
			MXT1386_LOG(KERN_INFO "Debug data read loop, reading %d bytes...\n", __func__,
				read_size);
			error = mxt_read_block(mxt->client, 
					       debug_data_addr, 
					       read_size, 
					       (u8 *) &data[offset]);
			if (error < 0) {
				MXT1386_LOG(KERN_INFO "Error reading debug data\n", __func__);
				goto error;
			}
			offset += read_size/2;
			size -= read_size;

			/* Select next page */
			error = mxt_write_byte(mxt->client, diagnostics_reg, 
					MXT_CMD_T6_PAGE_UP);
			if (error < 0) {
				MXT1386_LOG(KERN_INFO "Error writing to maXTouch device!\n", __func__);
				goto error;
			}
		}
		mutex_unlock(&mxt->debug_mutex);
	}

	buf_start = buf;
	i = mxt->current_debug_datap;

	while (((buf- buf_start) < (count - 6)) && 
		(i < mxt->device_info.num_nodes)){

		mxt->current_debug_datap++;
		if (debug_command == MXT_CMD_T6_REFERENCES_MODE)
			buf += sprintf(buf, "%d: %5d\n", i,
				       (u16) le16_to_cpu(data[i]));
		else if (debug_command == MXT_CMD_T6_DELTAS_MODE)
			buf += sprintf(buf, "%d: %5d\n", i,
				       (s16) le16_to_cpu(data[i]));
		i++;
	}

	return (buf - buf_start);
error:
	mutex_unlock(&mxt->debug_mutex);
	return error;
}

ssize_t deltas_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	return debug_data_read(file->private_data, buf, count, ppos, 
			       MXT_CMD_T6_DELTAS_MODE);
}

ssize_t refs_read(struct file *file, char *buf, size_t count, 
			loff_t *ppos)
{
	return debug_data_read(file->private_data, buf, count, ppos, 
			       MXT_CMD_T6_REFERENCES_MODE);
}

int debug_data_open(struct inode *inode, struct file *file)
{
	struct mxt_data *mxt;
	int i;
	mxt = inode->i_private;
	if (mxt == NULL)
		return -EIO;
	mxt->current_debug_datap = 0;
	mxt->debug_data = kmalloc(mxt->device_info.num_nodes * sizeof(u16),
				  GFP_KERNEL);
	if (mxt->debug_data == NULL)
		return -ENOMEM;

	
	for (i = 0; i < mxt->device_info.num_nodes; i++)
		mxt->debug_data[i] = 7777;
	

	file->private_data = mxt;
	return 0;
}

int debug_data_release(struct inode *inode, struct file *file)
{
	struct mxt_data *mxt;
	mxt = file->private_data;
	kfree(mxt->debug_data);
	return 0;
}

static struct file_operations delta_fops = {
	.owner = THIS_MODULE,
	.open = debug_data_open,
	.release = debug_data_release,
	.read = deltas_read,
};

static struct file_operations refs_fops = {
	.owner = THIS_MODULE,
	.open = debug_data_open,
	.release = debug_data_release,
	.read = refs_read,
};


int mxt_memory_open(struct inode *inode, struct file *file)
{
	struct mxt_data *mxt;
	mxt = container_of(inode->i_cdev, struct mxt_data, cdev);
	if (mxt == NULL)
		return -EIO;
	file->private_data = mxt;
	return 0;
}

int mxt_message_open(struct inode *inode, struct file *file)
{
	struct mxt_data *mxt;
	mxt = container_of(inode->i_cdev, struct mxt_data, cdev_messages);
	if (mxt == NULL)
		return -EIO;
	file->private_data = mxt;
	return 0;
}


ssize_t mxt_memory_read(struct file *file, char *buf, size_t count, 
			loff_t *ppos)
{
	int i;
	struct mxt_data *mxt;

	mxt = file->private_data;
	if (mxt->valid_ap){
		MXT1386_LOG(KERN_INFO "Reading %d bytes from current ap\n", __func__,
			  (int) count);
		i = mxt_read_block_wo_addr(mxt->client, count, (u8 *) buf);
	} else {
		MXT1386_LOG(KERN_INFO "Address pointer changed since set;"
			  "writing AP (%d) before reading %d bytes", __func__, 
			  mxt->address_pointer, (int) count);
		i = mxt_read_block(mxt->client, mxt->address_pointer, count,
			           buf);
	}
			
	return i;
}

ssize_t mxt_memory_write(struct file *file, const char *buf, size_t count,
			 loff_t *ppos)
{
	int i;
	int whole_blocks;
	int last_block_size;
	struct mxt_data *mxt;
	u16 address;
	
	mxt = file->private_data;
	address = mxt->address_pointer;

	MXT1386_LOG(KERN_INFO "mxt_memory_write entered\n", __func__);
	whole_blocks = count / I2C_PAYLOAD_SIZE;
	last_block_size = count % I2C_PAYLOAD_SIZE;

	for (i = 0; i < whole_blocks; i++) {
		MXT1386_LOG(KERN_INFO "About to write to %d...\n", __func__, 
			address);
		mxt_write_block(mxt->client, address, I2C_PAYLOAD_SIZE, 
				(u8 *) buf);
		address += I2C_PAYLOAD_SIZE;
		buf += I2C_PAYLOAD_SIZE;
	}

	mxt_write_block(mxt->client, address, last_block_size, (u8 *) buf);

	return count;
}


#define MXT_SET_ADDRESS  0
#define MXT_RESET  1
#define MXT_CALIBRATE  2
#define MXT_BACKUP  3
#define MXT_NONTOUCH_MSG  4
#define MXT_ALL_MSG  5

static int mxt_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	int retval;
	struct mxt_data *mxt;

	retval = 0;
	mxt = file->private_data;

	switch (cmd) {
	case MXT_SET_ADDRESS:
		retval = mxt_write_ap(mxt, (u16) arg);
		if (retval >= 0) {
			mxt->address_pointer = (u16) arg;
			mxt->valid_ap = 1;
		}
		break;
	case MXT_RESET:
		retval = mxt_write_byte(mxt->client,
			      MXT_BASE_ADDR(MXT_GEN_COMMANDPROCESSOR_T6, mxt) +
			      MXT_ADR_T6_RESET,
			      1);
		break;
	case MXT_CALIBRATE:
		retval = mxt_write_byte(mxt->client,
			      MXT_BASE_ADDR(MXT_GEN_COMMANDPROCESSOR_T6, mxt) +
			      MXT_ADR_T6_CALIBRATE,
			      1);

		break;
	case MXT_BACKUP:
		retval = mxt_write_byte(mxt->client,
			      MXT_BASE_ADDR(MXT_GEN_COMMANDPROCESSOR_T6, mxt) +
			      MXT_ADR_T6_BACKUPNV,
			      MXT_CMD_T6_BACKUP);
		break;
	case MXT_NONTOUCH_MSG:
		mxt->nontouch_msg_only = 1;
		break;
	case MXT_ALL_MSG:
		mxt->nontouch_msg_only = 0;
		break;
	default:
		return -EIO;
	}

	return retval;
} 

/*
 * Copies messages from buffer to user space.
 *
 * NOTE: if less than (mxt->message_size * 5 + 1) bytes requested,
 * this will return 0!
 * 
 */
ssize_t mxt_message_read(struct file *file, char *buf, size_t count, 
			 loff_t *ppos)
{
	int i;
	struct mxt_data *mxt;
	char *buf_start;
	
	mxt = file->private_data;
	if (mxt == NULL)
		return -EIO;
	buf_start = buf;

	mutex_lock(&mxt->msg_mutex);
	/* Copy messages until buffer empty, or 'count' bytes written */
	while ((mxt->msg_buffer_startp != mxt->msg_buffer_endp) &&
	       ((buf - buf_start) < (count - 5 * mxt->message_size - 1))){

		for (i = 0; i < mxt->message_size; i++){
			buf += sprintf(buf, "[%2X] ",
				*(mxt->messages + mxt->msg_buffer_endp *
					mxt->message_size + i));
		}
		buf += sprintf(buf, "\n");
		if (mxt->msg_buffer_endp < MXT_MESSAGE_BUFFER_SIZE)
			mxt->msg_buffer_endp++;
		else
			mxt->msg_buffer_endp = 0;
	}
	mutex_unlock(&mxt->msg_mutex);
	return (buf - buf_start);
}

static struct file_operations mxt_message_fops = {
	.owner = THIS_MODULE,
	.open = mxt_message_open,
	.read = mxt_message_read,
};

static struct file_operations mxt_memory_fops = {
	.owner = THIS_MODULE,
	.open = mxt_memory_open,
	.read = mxt_memory_read,
	.write = mxt_memory_write,
	.ioctl = mxt_ioctl,
};


/* Writes the address pointer (to set up following reads). */

int mxt_write_ap(struct mxt_data *mxt, u16 ap)
{
	struct i2c_client *client;
	__le16	le_ap = cpu_to_le16(ap);
	client = mxt->client;
	if (mxt != NULL)
		mxt->last_read_addr = -1;
	if (i2c_master_send(client, (u8 *) &le_ap, 2) == 2) {
		MXT1386_LOG(KERN_INFO "Address pointer set to %d\n", __func__, ap);
		return 0;
	} else {
		MXT1386_LOG(KERN_INFO "Error writing address pointer!\n", __func__);
		return -EIO;
	}
}

/* Calculates the 24-bit CRC sum. */
static u32 CRC_24(u32 crc, u8 byte1, u8 byte2)
{
	static const u32 crcpoly = 0x80001B;
	u32 result;
	u32 data_word;

	data_word = ((((u16) byte2) << 8u) | byte1);
	result = ((crc << 1u) ^ data_word);
	if (result & 0x1000000)
		result ^= crcpoly;
	return result;
}

/* Returns object address in mXT chip, or zero if object is not found */
static u16 get_object_address(uint8_t object_type,
			      uint8_t instance,
			      struct mxt_object *object_table,
			      int max_objs)
{
	uint8_t object_table_index = 0;
	uint8_t address_found = 0;
	uint16_t address = 0;
	struct mxt_object *obj;

	while ((object_table_index < max_objs) && !address_found) {
		obj = &object_table[object_table_index];
		if (obj->type == object_type) {
			address_found = 1;
			/* Are there enough instances defined in the FW? */
			if (obj->instances >= instance) {
				address = obj->chip_addr +
					  (obj->size + 1) * instance;
			} else {
				return 0;
			}
		}
		object_table_index++;
	}
	return address;
}


/*
 * Reads a block of bytes from given address from mXT chip. If we are
 * reading from message window, and previous read was from message window,
 * there's no need to write the address pointer: the mXT chip will
 * automatically set the address pointer back to message window start.
 */

static int mxt_read_block(struct i2c_client *client,
		   u16 addr,
		   u16 length,
		   u8 *value)
{
	struct i2c_adapter *adapter = client->adapter;
	struct i2c_msg msg[2];
	__le16	le_addr;
	struct mxt_data *mxt;

	mxt = i2c_get_clientdata(client);	
	
	if (mxt != NULL) {
		MXT1386_LOG(KERN_INFO "%s: msg addr test, mxt->msg_proc_addr = 0x%x, mxt->last_read_addr = 0x%x, current addr = 0x%x\n", 
		__func__, mxt->msg_proc_addr, mxt->last_read_addr, addr);
		
		if ((mxt->last_read_addr == addr) &&
			(addr == mxt->msg_proc_addr)) {
			if (i2c_master_recv(client, value, length) == length) {
				MXT1386_LOG(KERN_INFO "%s: Host is reading from chip, addr = 0x%x, length = %d, *value = %d\n", 
					__func__, addr, length, *value);
				return length;
			}				
			else {
				MXT1386_LOG(KERN_INFO "%s: Host read failed!\n", __func__);
				return -EIO;
			}				
		} else {
			mxt->last_read_addr = addr;
		}
	}

	le_addr = cpu_to_le16(addr);
	MXT1386_LOG(KERN_INFO "%s: Writing address pointer & reading, addr = 0x%02x, , le_addr = 0x%02x, length = %d, *value = %d\n", 
			__func__, addr, le_addr, length, *value);
	
	msg[0].addr  = client->addr;
	msg[0].flags = 0x00;
	msg[0].len   = 2;
	msg[0].buf   = (u8 *) &le_addr;

	msg[1].addr  = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len   = length;
	msg[1].buf   = (u8 *) value;
	if  (i2c_transfer(adapter, msg, 2) == 2)
		return length;
	else
		return -EIO;
}

/* Reads a block of bytes from current address from mXT chip. */
static int mxt_read_block_wo_addr(struct i2c_client *client,
			   u16 length,
			   u8 *value)
{
	if  (i2c_master_recv(client, value, length) == length) {
		MXT1386_LOG(KERN_INFO "%s: I2C block read ok\n", __func__);
		return length;
	} else {
		MXT1386_LOG(KERN_INFO "%s: I2C block read failed\n", __func__);
		return -EIO;
	}
}

/* Writes one byte to given address in mXT chip. */
static int mxt_write_byte(struct i2c_client *client, u16 addr, u8 value)
{
	struct {
		__le16 le_addr;
		u8 data;

	} i2c_byte_transfer;

	struct mxt_data *mxt;

	mxt = i2c_get_clientdata(client);
	if (mxt != NULL)
		mxt->last_read_addr = -1;
	i2c_byte_transfer.le_addr = cpu_to_le16(addr);
	i2c_byte_transfer.data = value;
	if  (i2c_master_send(client, (u8 *) &i2c_byte_transfer, 3) == 3)
		return 0;
	else
		return -EIO;
}

/* Writes a block of bytes (max 256) to given address in mXT chip. */
static int mxt_write_block(struct i2c_client *client,
		    u16 addr,
		    u16 length,
		    u8 *value)
{
	int i;
	struct {
		__le16	le_addr;
		u8	data[256];

	} i2c_block_transfer;

	struct mxt_data *mxt;

	MXT1386_LOG("Writing %d bytes to %d...\n", length, addr);
	if (length > 256)
		return -EINVAL;
	mxt = i2c_get_clientdata(client);
	if (mxt != NULL)
		mxt->last_read_addr = -1;
	for (i = 0; i < length; i++)
		i2c_block_transfer.data[i] = *value++;
	i2c_block_transfer.le_addr = cpu_to_le16(addr);
	i = i2c_master_send(client, (u8 *) &i2c_block_transfer, length + 2);
	if (i == (length + 2))
		return length;
	else
		return -EIO;
}

/* Calculates the CRC value for mXT infoblock. */
int calculate_infoblock_crc(u32 *crc_result, u8 *data, int crc_area_size)
{
	u32 crc = 0;
	int i;

	for (i = 0; i < (crc_area_size - 1); i = i + 2)
		crc = CRC_24(crc, *(data + i), *(data + i + 1));
	/* If uneven size, pad with zero */
	if (crc_area_size & 0x0001)
		crc = CRC_24(crc, *(data + i), 0);
	/* Return only 24 bits of CRC. */
	*crc_result = (crc & 0x00FFFFFF);

	return 0;
}

int find_touch_key_value( struct mxt_data *mxt, int status, u16 ypos)
{
    int i = 0;
    for(i=0; i<ARRAY_SIZE(key_info); i++)
    {
        if((ypos > key_info[i].start) && (ypos < key_info[i].end))
        {          
           printk("%s key_code = %d \n",__func__,key_info[i].code);
            if(status & MXT_MSGB_T9_DETECT)
            {
                input_report_key(mxt->input, key_info[i].code, 1);
                input_sync(mxt->input);
                last_key = key_info[i].code;
            }
            else
            {
                printk("%s key_code = %d relase\n",__func__,key_info[i].code);
                input_report_key(mxt->input, key_info[i].code, 0);
                input_sync(mxt->input);
                last_key = 0;

            }           
            break;
        }
    }
   

    return 0;    
}


void process_T9_message(u8 *message, struct mxt_data *mxt)
{
	struct	input_dev *input;
	u8  status;
	u16 xpos = 0xFFFF;
	u16 ypos = 0xFFFF;
	u8  touch_size = 255;
	u8  touch_number;
	u8  amplitude;
	u8  report_id;
    u32 key_code = 0;
    
	input = mxt->input;
	status = message[MXT_MSG_T9_STATUS];
	report_id = message[0];

	if (status & MXT_MSGB_T9_SUPPRESS) {
		/* Touch has been suppressed by grip/face */
		/* detection                              */
		MXT1386_LOG(KERN_INFO "SUPRESS\n", __func__);
	} else {
		MXT1386_LOG("XPOSMSB = 0x%02x, YPOSMSB = 0x%02x, XYPOSLSB = 0x%02x\n",
			message[MXT_MSG_T9_XPOSMSB], message[MXT_MSG_T9_YPOSMSB], message[MXT_MSG_T9_XYPOSLSB]);
		
		xpos = message[MXT_MSG_T9_XPOSMSB] * 16 +
			((message[MXT_MSG_T9_XYPOSLSB] >> 4) & 0xF);
		ypos = message[MXT_MSG_T9_YPOSMSB] * 16 +
			((message[MXT_MSG_T9_XYPOSLSB] >> 0) & 0xF);

		MXT1386_LOG("Before Reverse: xpos = %d, ypos = %d\n", xpos, ypos);
		#if 0 //xhh
		//颠倒X坐标
		if( xpos < CONFIG_ATMEL_MXT1386_MAX_X)
			xpos = CONFIG_ATMEL_MXT1386_MAX_X - xpos;
		else
			xpos = 0;

		//颠倒Y坐标
		if( ypos < CONFIG_ATMEL_MXT1386_MAX_Y)
			ypos = CONFIG_ATMEL_MXT1386_MAX_Y - ypos;
		else
			ypos = 0;
		#endif
        
        #if TOUCH_KEY_EN
		if( xpos < 8 && xpos >= 0)
		{
            find_touch_key_value(mxt, status, ypos); 
            return;
        } 
        else if(last_key)
        {
            
            printk("%s key_code = %d relase\n",__func__,last_key);
            input_report_key(mxt->input, last_key, 0);
            input_sync(mxt->input);
            last_key = 0;  
        }
		#endif	
        
		MXT1386_LOG("After Reverse: xpos = %d, ypos = %d\n", xpos, ypos);

		touch_number = message[MXT_MSG_REPORTID] -
			mxt->rid_map[report_id].first_rid;

		if (status & MXT_MSGB_T9_DETECT) {
			/*
			 * TODO: more precise touch size calculation?
			 * mXT224 reports the number of touched nodes,
			 * so the exact value for touch ellipse major
			 * axis length would be 2*sqrt(touch_size/pi)
			 * (assuming round touch shape).
			 */
			MXT1386_LOG(KERN_INFO " ----process_T9_message --MXT_MSGB_T9_DETECT--- \n", __func__);
			touch_size = message[MXT_MSG_T9_TCHAREA];
			touch_size = touch_size >> 2;
			if (!touch_size)
				touch_size = 1;
			report_mt(touch_number, touch_size, xpos, ypos, mxt);
			if (status & MXT_MSGB_T9_AMP)
				/* Amplitude of touch has changed */
				amplitude = message[MXT_MSG_T9_TCHAMPLITUDE];
		} else if (status & MXT_MSGB_T9_RELEASE) {
			/* The previously reported touch has been removed.*/
			MXT1386_LOG(KERN_INFO " ----process_T9_message --MXT_MSGB_T9_RELEASE--- \n", __func__);
			report_mt(touch_number, 0, xpos, ypos, mxt);
		}
		#ifdef EFFICIENT_REPORT
		#else
		report_sync(mxt);
		#endif
	}
	
	if (status & MXT_MSGB_T9_SUPPRESS) {
		MXT1386_LOG(KERN_INFO "SUPRESS", __func__);
	} else {
		if (status & MXT_MSGB_T9_DETECT) {
			MXT1386_LOG(KERN_INFO "DETECT:%s%s%s%s", __func__, 
				((status & MXT_MSGB_T9_PRESS) ? " PRESS" : ""), 
				((status & MXT_MSGB_T9_MOVE) ? " MOVE" : ""), 
				((status & MXT_MSGB_T9_AMP) ? " AMP" : ""), 
				((status & MXT_MSGB_T9_VECTOR) ? " VECT" : ""));

		} else if (status & MXT_MSGB_T9_RELEASE) {
			MXT1386_LOG(KERN_INFO "RELEASE");
		}
	}
	MXT1386_LOG(KERN_INFO "X=%d, Y=%d, TOUCHSIZE=%d", __func__,
		xpos, ypos, touch_size);

	return;
}

int process_message(u8 *message, u8 object, struct mxt_data *mxt)
{
	struct i2c_client *client;
	u8  status;
	u16 xpos = 0xFFFF;
	u16 ypos = 0xFFFF;
	u8  event;
	u8  direction;
	u16 distance;
	u8  length;
	u8  report_id;
    int  keyIndex;

	client = mxt->client;
	length = mxt->message_size;
	report_id = message[0];

	if ((mxt->nontouch_msg_only == 0) ||
	    (!IS_TOUCH_OBJECT(object))){
		mutex_lock(&mxt->msg_mutex);
		/* Copy the message to buffer */
		if (mxt->msg_buffer_startp < MXT_MESSAGE_BUFFER_SIZE) {
			mxt->msg_buffer_startp++;
		} else {
			mxt->msg_buffer_startp = 0;
		}
		
		if (mxt->msg_buffer_startp == mxt->msg_buffer_endp) {
			MXT1386_LOG(KERN_INFO "Message buf full, discarding last entry.\n", __func__);
			if (mxt->msg_buffer_endp < MXT_MESSAGE_BUFFER_SIZE) {
				mxt->msg_buffer_endp++;
			} else {
				mxt->msg_buffer_endp = 0;
			}
		}
		memcpy((mxt->messages + mxt->msg_buffer_startp * length), 
		       message,
		       length);
		mutex_unlock(&mxt->msg_mutex);
	}

	switch (object) {
	case MXT_GEN_COMMANDPROCESSOR_T6:
		status = message[1];
		if (status & MXT_MSGB_T6_COMSERR) {
			dev_err(&client->dev,
				"maXTouch checksum error\n");
		}
		if (status & MXT_MSGB_T6_CFGERR) {
			/* 
			 * Configuration error. A proper configuration
			 * needs to be written to chip and backed up. Refer
			 * to protocol document for further info.
			 */
			dev_err(&client->dev,
				"maXTouch configuration error\n");
		}
		if (status & MXT_MSGB_T6_CAL) {
			/* Calibration in action, no need to react */
			dev_info(&client->dev,
				"maXTouch calibration in progress\n");
		}
		if (status & MXT_MSGB_T6_SIGERR) {
			/* 
			 * Signal acquisition error, something is seriously
			 * wrong, not much we can in the driver to correct
			 * this
			 */
			dev_err(&client->dev,
				"maXTouch acquisition error\n");
		}
		if (status & MXT_MSGB_T6_OFL) {
			/*
			 * Cycle overflow, the acquisition is too short.
			 * Can happen temporarily when there's a complex
			 * touch shape on the screen requiring lots of
			 * processing.
			 */
			dev_err(&client->dev,
				"maXTouch cycle overflow\n");
		}
		if (status & MXT_MSGB_T6_RESET) {
			/* Chip has reseted, no need to react. */
			dev_info(&client->dev,
				"maXTouch chip reset\n");
		}
		if (status == 0) {
			/* Chip status back to normal. */
			dev_info(&client->dev,
				"maXTouch status normal\n");
		}
		break;

	case MXT_TOUCH_MULTITOUCHSCREEN_T9:
		process_T9_message(message, mxt);
		break;

	case MXT_SPT_GPIOPWM_T19:
		if (debug >= DEBUG_TRACE)
			dev_info(&client->dev,
				"Receiving GPIO message\n");
		break;

	case MXT_PROCI_GRIPFACESUPPRESSION_T20:
		if (debug >= DEBUG_TRACE)
			dev_info(&client->dev,
				"Receiving face suppression msg\n");
		break;

	case MXT_PROCG_NOISESUPPRESSION_T22:
		printk("--------- MXT_PROCG_NOISESUPPRESSION_T22 enter! ---------\n");
		//if (debug >= DEBUG_TRACE)
			dev_info(&client->dev,
				"Receiving noise suppression msg\n");
		status = message[MXT_MSG_T22_STATUS];
		if (status & MXT_MSGB_T22_FHCHG) {
			//if (debug >= DEBUG_TRACE)
				dev_info(&client->dev,
					"maXTouch: Freq changed\n");
		}
		if (status & MXT_MSGB_T22_GCAFERR) {
			//if (debug >= DEBUG_TRACE)
				dev_info(&client->dev,
					"maXTouch: High noise "
					"level\n");
		}
		if (status & MXT_MSGB_T22_FHERR) {
			//if (debug >= DEBUG_TRACE)
				dev_info(&client->dev,
					"maXTouch: Freq changed - "
					"Noise level too high\n");
		}
		break;

	case MXT_PROCI_ONETOUCHGESTUREPROCESSOR_T24:
		if (debug >= DEBUG_TRACE)
			dev_info(&client->dev,
				"Receiving one-touch gesture msg\n");

		event = message[MXT_MSG_T24_STATUS] & 0x0F;
		xpos = message[MXT_MSG_T24_XPOSMSB] * 16 +
			((message[MXT_MSG_T24_XYPOSLSB] >> 4) & 0x0F);
		ypos = message[MXT_MSG_T24_YPOSMSB] * 16 +
			((message[MXT_MSG_T24_XYPOSLSB] >> 0) & 0x0F);
		xpos >>= 2;
		ypos >>= 2;
		direction = message[MXT_MSG_T24_DIR];
		distance = message[MXT_MSG_T24_DIST] +
			   (message[MXT_MSG_T24_DIST + 1] << 16);

		report_gesture((event << 24) | (direction << 16) | distance,
			mxt);
		report_gesture((xpos << 16) | ypos, mxt);

		break;

	case MXT_SPT_SELFTEST_T25:
		if (debug >= DEBUG_TRACE)
			dev_info(&client->dev,
				"Receiving Self-Test msg\n");

		if (message[MXT_MSG_T25_STATUS] == MXT_MSGR_T25_OK) {
			if (debug >= DEBUG_TRACE)
				dev_info(&client->dev,
					"maXTouch: Self-Test OK\n");

		} else  {
			dev_err(&client->dev,
				"maXTouch: Self-Test Failed [%02x]:"
				"{%02x,%02x,%02x,%02x,%02x}\n",
				message[MXT_MSG_T25_STATUS],
				message[MXT_MSG_T25_STATUS + 0],
				message[MXT_MSG_T25_STATUS + 1],
				message[MXT_MSG_T25_STATUS + 2],
				message[MXT_MSG_T25_STATUS + 3],
				message[MXT_MSG_T25_STATUS + 4]
				);
		}
		break;

	case MXT_PROCI_TWOTOUCHGESTUREPROCESSOR_T27:
		if (debug >= DEBUG_TRACE)
			dev_info(&client->dev,
				"Receiving 2-touch gesture message\n");

		event = message[MXT_MSG_T27_STATUS] & 0xF0;
		xpos = message[MXT_MSG_T27_XPOSMSB] * 16 +
			((message[MXT_MSG_T27_XYPOSLSB] >> 4) & 0x0F);
		ypos = message[MXT_MSG_T27_YPOSMSB] * 16 +
			((message[MXT_MSG_T27_XYPOSLSB] >> 0) & 0x0F);
		xpos >>= 2;
		ypos >>= 2;
		direction = message[MXT_MSG_T27_ANGLE];
		distance = message[MXT_MSG_T27_SEPARATION] +
			   (message[MXT_MSG_T27_SEPARATION + 1] << 16);

		report_gesture((event << 24) | (direction << 16) | distance,
			mxt);
		report_gesture((xpos << 16) | ypos, mxt);


		break;

	case MXT_SPT_CTECONFIG_T28:
		if (debug >= DEBUG_TRACE)
			dev_info(&client->dev,
				"Receiving CTE message...\n");
		status = message[MXT_MSG_T28_STATUS];
		if (status & MXT_MSGB_T28_CHKERR)
			dev_err(&client->dev,
				"maXTouch: Power-Up CRC failure\n");

		break;
        
	default:
		if (debug >= DEBUG_TRACE)
			dev_info(&client->dev,
				"maXTouch: Unknown message!\n");

		break;
	}

	return 0;
}


/*
 * Processes messages when the interrupt line (CHG) is asserted. Keeps
 * reading messages until a message with report ID 0xFF is received,
 * which indicates that there is no more new messages.
 *
 */

static void ts_thread_1386(struct work_struct *work)
{
	struct	mxt_data *mxt;
	struct	i2c_client *client;

	u8	*message;
	u16	message_length;
	u16	message_addr;
	u8	report_id = 0;
	u8	object = 0;
	#ifdef EFFICIENT_REPORT
	u8 	has_multitouch_msg = 0;
	#endif
	int	error;
	int	i;
	char    *message_string;
	char    *message_start;
	static u32 count;

	message = NULL;
	//mxt = container_of(work, struct mxt_data, dwork.work);
	mxt = atmel_mxt;
	//disable_irq(mxt->irq);
	client = mxt->client;
	message_addr = 	mxt->msg_proc_addr;
	message_length = mxt->message_size;

	MXT1386_LOG(KERN_INFO "%s: ts_thread_1386 entry %d times!\n", __func__, ++count);

	if (message_length < 256) {
		message = kmalloc(message_length, GFP_KERNEL);
		if (message == NULL) {
			dev_err(&client->dev, "Error allocating memory\n");
			return;
		}
	} else {
		dev_err(&client->dev,
			"Message length larger than 256 bytes not supported\n");
		return;
	}

	do {
		/* Read next message, reread on failure. */
		mxt->message_counter++;
		for (i = 1; i < I2C_RETRY_COUNT; i++) {
			error = mxt_read_block(client,
					       message_addr,
					       message_length,
					       message);
			if (error >= 0)
				break;
			mxt->read_fail_counter++;
			dev_err(&client->dev,
				"Failure reading maxTouch device\n");
		}
		if (error < 0) {
			kfree(message);
			return;
		}
		
		if (mxt->address_pointer != message_addr)
			mxt->valid_ap = 0;
		report_id = message[0];

		if (debug >= DEBUG_RAW) {
			MXT1386_LOG(KERN_INFO "%s: %s message [msg count: %08x]:", __func__,
				  REPORT_ID_TO_OBJECT_NAME(report_id, mxt),
				  mxt->message_counter
			);
			/* 5 characters per one byte */
			message_string = kmalloc(message_length * 5, 
						 GFP_KERNEL);
			if (message_string == NULL) {
				dev_err(&client->dev, 
					"Error allocating memory\n");
				kfree(message);
				return;
			}
			message_start = message_string;
			for (i = 0; i < message_length; i++) {
				message_string += 
					sprintf(message_string, 
						"0x%02X ", message[i]);
			}
			MXT1386_LOG(KERN_INFO "%s", message_start, __func__);
			kfree(message_start);
		}

		if ((report_id != MXT_END_OF_MESSAGES) && (report_id != 0)) {
			memcpy(mxt->last_message, message, message_length);
			mxt->new_msgs = 1;
			smp_wmb();
			/* Get type of object and process the message */
			object = mxt->rid_map[report_id].object;
			
			MXT1386_LOG(KERN_INFO "current value: mxt->rid_map[%d].object = %d, object = %d\n",
				__func__, report_id, mxt->rid_map[report_id].object, object);
			mxt_buf_test(message, message_length);
			
			process_message(message, object, mxt);
			MXT1386_LOG(KERN_INFO "trace 0\n", __func__);
		}
		
		#ifdef EFFICIENT_REPORT
		if(object == MXT_TOUCH_MULTITOUCHSCREEN_T9)
		{
			has_multitouch_msg = 1;
		}
		else if( has_multitouch_msg == 1)
		{
			has_multitouch_msg = 0;
			report_sync(mxt);
		}
		object = 0;
		#endif		

	} while (comms ? (mxt->read_chg() == 0) : 
		((report_id != MXT_END_OF_MESSAGES) && (report_id != 0)));
	//} while ((report_id != MXT_END_OF_MESSAGES) && (report_id != 0));

	/*
	#ifdef EFFICIENT_REPORT
	report_sync(mxt);
	#endif
	*/
	
	kfree(message);

	MXT1386_LOG(KERN_INFO "%s: Now will enable the irq, ts_irq = %d\n", __func__, ts_irq);
	enable_irq(mxt->irq);
    //printk("%s, int_pin = %d \n",__func__,gpio_get_value(client->irq));
}

/*
 * The maXTouch device will signal the host about a new message by asserting
 * the CHG line. This ISR schedules a worker routine to read the message when
 * that happens.
 */
 
//static struct workqueue_struct *queue = NULL;
//static struct work_struct work;

static irqreturn_t mxt_irq_handler(int irq, void *dev_id)
{	
	struct mxt_data *mxt = (struct mxt_data *)dev_id;
    struct i2c_client *client = mxt->client;
	int int_value = client->irq;
	static u8 wakeup_flag;
	static u32 counter;
	/* 目前TP的中断行为如下: 当LCD进入standby模式后，背光控制脚LCDC_CS将拉低，此时LCDC_CS控制的背光升压输入端（DCtoDC输入端）TPVCC也将被拉低，
	使背光关闭；由于TPVCC同时也作为TP的电源输入端，此时系统若在TPVCC为低电平的情况下进入TP的ISR函数，TP设备将不应答，并且将不被唤醒。 因此，
	在此增加一个静态标志，判断当LCDS脚为低时，不启动工作队列，同时将wakeup_flag标志置1，下次进入中断时，不再禁止中断，直接启动工作队列*/
	
#if 0
	MXT1386_LOG(KERN_INFO "mxt_irq_handler() IN\n");

	mxt->irq_counter++;
	if (mxt->valid_interrupt()) {
		/* Send the signal only if falling edge generated the irq. */
		cancel_delayed_work(&mxt->dwork);
		schedule_delayed_work(&mxt->dwork, 0);
		mxt->valid_irq_counter++;
	} else {
		mxt->invalid_irq_counter++;
		return IRQ_NONE;
	}

	MXT1386_LOG(KERN_INFO "-------------- Be carefull MXC_touchscreen interrupt comes! --------------\n", __func__);
	MXT1386_LOG(KERN_INFO "interrupt counter road\n");
	disable_irq_nosync(mxt->irq);

	queue_work(mxt->queue,&mxt->work);
	
#else

	MXT1386_LOG(KERN_INFO "-------------- Be carefull MXC_touchscreen interrupt comes! --------------\n", __func__);
	MXT1386_LOG(KERN_INFO "ts_irq = %d, irq = %d, mxt->irq = %d, TOUCH_INT_PIN = %d\n", __func__, ts_irq, irq, mxt->irq, int_value);

	if(gpio_get_value(LCDC_STANDBY_PIN)) {
		MXT1386_LOG(KERN_INFO "%s: LCDC_STANDBY_PIN test, value is high\n", __func__);

		if (0==wakeup_flag) {
			disable_irq_nosync(mxt->irq);
		}
		else {
			wakeup_flag = 0;
		}
		
#if WORKQ_RIGHTNOW
		MXT1386_LOG(KERN_INFO "%s: queue_work start\n", __func__);
		queue_work(mxt->queue,&mxt->work);
#else
		MXT1386_LOG(KERN_INFO "%s: queue_delayed_work start\n", __func__);
		queue_delayed_work(mxt->queue, &mxt->dwork, 0);
#endif
	}
	else {
		MXT1386_LOG(KERN_INFO "%s: LCDC_STANDBY_PIN test, value is low\n", __func__);	
		wakeup_flag = 1;
		return IRQ_NONE;
	}
#endif
	return IRQ_HANDLED;
}

/******************************************************************************/
/* Initialization of driver                                                   */
/******************************************************************************/

static int __devinit mxt_identify(struct i2c_client *client,
				  struct mxt_data *mxt,
				  u8 *id_block_data)
{
	u8 buf[7];
	int error;
	int identified;

	identified = 0;

	/* Read Device info to check if chip is valid */
	error = mxt_read_block(client, MXT_ADDR_INFO_BLOCK, MXT_ID_BLOCK_SIZE,
			       (u8 *) buf);

	if (error < 0) {
		mxt->read_fail_counter++;
		dev_err(&client->dev, "Failure accessing maXTouch device\n");
		return -EIO;
	}

	memcpy(id_block_data, buf, MXT_ID_BLOCK_SIZE);

	mxt->device_info.family_id  = buf[0];
	mxt->device_info.variant_id = buf[1];
	mxt->device_info.major	    = ((buf[2] >> 4) & 0x0F);
	mxt->device_info.minor      = (buf[2] & 0x0F);
	mxt->device_info.build	    = buf[3];
	mxt->device_info.x_size	    = buf[4];
	mxt->device_info.y_size	    = buf[5];
	mxt->device_info.num_objs   = buf[6];
	mxt->device_info.num_nodes  = mxt->device_info.x_size *
				      mxt->device_info.y_size;

	/*
         * Check Family & Variant Info; warn if not recognized but
         * still continue.
         */

	/* MXT224 */
	if (mxt->device_info.family_id == MXT224_FAMILYID) {
		strcpy(mxt->device_info.family_name, "mXT224");

		if (mxt->device_info.variant_id == MXT224_CAL_VARIANTID) {
			strcpy(mxt->device_info.variant_name, "Calibrated");
		} else if (mxt->device_info.variant_id == 
			MXT224_UNCAL_VARIANTID) {
			strcpy(mxt->device_info.variant_name, "Uncalibrated");
		} else {
			dev_err(&client->dev,
				"Warning: maXTouch Variant ID [%d] not "
				"supported\n",
				mxt->device_info.variant_id);
			strcpy(mxt->device_info.variant_name, "UNKNOWN");
			/* identified = -ENXIO; */
		}

	/* MXT1386 */
	} else if (mxt->device_info.family_id == MXT1386_FAMILYID) {
		strcpy(mxt->device_info.family_name, "mXT1386");

		if (mxt->device_info.variant_id == MXT1386_CAL_VARIANTID) {
			strcpy(mxt->device_info.variant_name, "Calibrated");
		} else {
			dev_err(&client->dev,
				"Warning: maXTouch Variant ID [%d] not "
				"supported\n",
				mxt->device_info.variant_id);
			strcpy(mxt->device_info.variant_name, "UNKNOWN");
			/* identified = -ENXIO; */
		}
	/* Unknown family ID! */
	} else {
		dev_err(&client->dev,
			"Warning: maXTouch Family ID [%d] not supported\n",
			mxt->device_info.family_id);
		strcpy(mxt->device_info.family_name, "UNKNOWN");
		strcpy(mxt->device_info.variant_name, "UNKNOWN");
		/* identified = -ENXIO; */
	}

	dev_info(
		&client->dev,
		"Atmel maXTouch (Family %s (%X), Variant %s (%X)) Firmware "
		"version [%d.%d] Build %d, num_objs = %d\n",
		mxt->device_info.family_name,
		mxt->device_info.family_id,
		mxt->device_info.variant_name,
		mxt->device_info.variant_id,
		mxt->device_info.major,
		mxt->device_info.minor,
		mxt->device_info.build,
		mxt->device_info.num_objs
	);
	dev_info(
		&client->dev,
		"Atmel maXTouch Configuration "
		"[X: %d] x [Y: %d]\n",
		mxt->device_info.x_size,
		mxt->device_info.y_size
	);
	return identified;
}

/*
 * Reads the object table from maXTouch chip to get object data like
 * address, size, report id. For Info Block CRC calculation, already read
 * id data is passed to this function too (Info Block consists of the ID
 * block and object table).
 *
 */
static int __devinit mxt_read_object_table(struct i2c_client *client,
					   struct mxt_data *mxt,
					   u8 *raw_id_data)
{
	u16	report_id_count;
	u8	buf[MXT_OBJECT_TABLE_ELEMENT_SIZE];
	u8      *raw_ib_data;
	u8	object_type;
	u16	object_address;
	u16	object_size;
	u8	object_instances;
	u8	object_report_ids;
	u16	object_info_address;
	u32	crc;
	u32     calculated_crc;
	int	i;
	int	error;

	u8	object_instance;
	u8	object_report_id;
	u8	report_id;
	int     first_report_id;
	int     ib_pointer;
	struct mxt_object *object_table;

	MXT1386_LOG(KERN_INFO "maXTouch driver reading configuration\n", __func__);

	object_table = kzalloc(sizeof(struct mxt_object) *
			       mxt->device_info.num_objs,
			       GFP_KERNEL);
	if (object_table == NULL) {
		MXT1386_LOG(KERN_INFO "maXTouch: Memory allocation failed!\n", __func__);
		error = -ENOMEM;
		goto err_object_table_alloc;
	}

	raw_ib_data = kmalloc(MXT_OBJECT_TABLE_ELEMENT_SIZE *
			mxt->device_info.num_objs + MXT_ID_BLOCK_SIZE,
			GFP_KERNEL);
	if (raw_ib_data == NULL) {
		MXT1386_LOG(KERN_INFO "maXTouch: Memory allocation failed!\n", __func__);
		error = -ENOMEM;
		goto err_ib_alloc;
	}

	/* Copy the ID data for CRC calculation. */
	memcpy(raw_ib_data, raw_id_data, MXT_ID_BLOCK_SIZE);
	ib_pointer = MXT_ID_BLOCK_SIZE;

	mxt->object_table = object_table;

	MXT1386_LOG(KERN_INFO "maXTouch driver Memory allocated\n", __func__);

	object_info_address = MXT_ADDR_OBJECT_TABLE;

	report_id_count = 0;
	for (i = 0; i < mxt->device_info.num_objs; i++) {
		MXT1386_LOG(KERN_INFO "Reading maXTouch at [0x%04x]: ", __func__,
			  object_info_address);

		error = mxt_read_block(client, object_info_address,
				       MXT_OBJECT_TABLE_ELEMENT_SIZE, buf);

		if (error < 0) {
			mxt->read_fail_counter++;
			dev_err(&client->dev,
				"maXTouch Object %d could not be read\n", __func__, i);
			error = -EIO;
			goto err_object_read;
		}

		memcpy(raw_ib_data + ib_pointer, buf, 
		       MXT_OBJECT_TABLE_ELEMENT_SIZE);
		ib_pointer += MXT_OBJECT_TABLE_ELEMENT_SIZE;

		object_type       =  buf[0];
		object_address    = (buf[2] << 8) + buf[1];
		object_size       =  buf[3] + 1;
		object_instances  =  buf[4] + 1;
		object_report_ids =  buf[5];
		MXT1386_LOG(KERN_INFO "Type=%03d, Address=0x%04x, "
			  "Size=0x%02x, %d instances, %d report id's\n", __func__,
			  object_type,
			  object_address,
			  object_size,
			  object_instances,
			  object_report_ids
		);

		/* TODO: check whether object is known and supported? */
		
		/* Save frequently needed info. */
		if (object_type == MXT_GEN_MESSAGEPROCESSOR_T5) {
			mxt->msg_proc_addr = object_address;
			mxt->message_size = object_size;
		}

		object_table[i].type            = object_type;
		object_table[i].chip_addr       = object_address;
		object_table[i].size            = object_size;
		object_table[i].instances       = object_instances;
		object_table[i].num_report_ids  = object_report_ids;
		report_id_count += object_instances * object_report_ids;

		object_info_address += MXT_OBJECT_TABLE_ELEMENT_SIZE;
	}

	mxt->rid_map =
		kzalloc(sizeof(struct report_id_map) * (report_id_count + 1),
			/* allocate for report_id 0, even if not used */
			GFP_KERNEL);
	if (mxt->rid_map == NULL) {
		MXT1386_LOG(KERN_INFO "maXTouch: Can't allocate memory!\n", __func__);
		error = -ENOMEM;
		goto err_rid_map_alloc;
	}

	mxt->messages = kzalloc(mxt->message_size * MXT_MESSAGE_BUFFER_SIZE,
				GFP_KERNEL);
	if (mxt->messages == NULL) {
		MXT1386_LOG(KERN_INFO "maXTouch: Can't allocate memory!\n", __func__);
		error = -ENOMEM;
		goto err_msg_alloc;
	}

	mxt->last_message = kzalloc(mxt->message_size, GFP_KERNEL);
	if (mxt->last_message == NULL) {
		MXT1386_LOG(KERN_INFO "maXTouch: Can't allocate memory!\n", __func__);
		error = -ENOMEM;
		goto err_msg_alloc;
	}

	mxt->report_id_count = report_id_count;
	if (report_id_count > 254) {	/* 0 & 255 are reserved */
			dev_err(&client->dev,
				"Too many maXTouch report id's [%d]\n",
				report_id_count);
			error = -ENXIO;
			goto err_max_rid;
	}

	/* Create a mapping from report id to object type */
	report_id = 1; /* Start from 1, 0 is reserved. */

	/* Create table associating report id's with objects & instances */
	for (i = 0; i < mxt->device_info.num_objs; i++) {
		for (object_instance = 0;
		     object_instance < object_table[i].instances;
		     object_instance++){
			first_report_id = report_id;
			for (object_report_id = 0;
			     object_report_id < object_table[i].num_report_ids;
			     object_report_id++) {
				mxt->rid_map[report_id].object =
					object_table[i].type;
				mxt->rid_map[report_id].instance =
					object_instance;
				mxt->rid_map[report_id].first_rid =
					first_report_id;
				report_id++;
			}
		}
	}

	/* Read 3 byte CRC */
	error = mxt_read_block(client, object_info_address, 3, buf);
	if (error < 0) {
		mxt->read_fail_counter++;
		dev_err(&client->dev, "Error reading CRC\n");
	}

	crc = (buf[2] << 16) | (buf[1] << 8) | buf[0];

	if (calculate_infoblock_crc(&calculated_crc, raw_ib_data,
				    ib_pointer)) {
		MXT1386_LOG(KERN_INFO "Error while calculating CRC!\n");
		calculated_crc = 0;
	}
	kfree(raw_ib_data);

	MXT1386_LOG(KERN_INFO "Reported info block CRC = 0x%6X\n", __func__, crc);
	MXT1386_LOG(KERN_INFO "Calculated info block CRC = 0x%6X\n\n", __func__, calculated_crc);
	
	if (crc == calculated_crc) {
		mxt->info_block_crc = crc;
	} else {
		mxt->info_block_crc = 0;
		MXT1386_LOG(KERN_INFO "maXTouch: Info block CRC invalid!\n"), __func__;
	}

	if (debug >= DEBUG_VERBOSE) {

		dev_info(&client->dev, "maXTouch: %d Objects\n",
				mxt->device_info.num_objs);

		for (i = 0; i < mxt->device_info.num_objs; i++) {
			dev_info(&client->dev, "Type:\t\t\t[%d]: %s\n",
				 object_table[i].type,
				 object_type_name[object_table[i].type]);
			dev_info(&client->dev, "\tAddress:\t0x%04X\n",
				object_table[i].chip_addr);
			dev_info(&client->dev, "\tSize:\t\t%d Bytes\n",
				 object_table[i].size);
			dev_info(&client->dev, "\tInstances:\t%d\n",
				 object_table[i].instances);
			dev_info(&client->dev, "\tReport Id's:\t%d\n",
				 object_table[i].num_report_ids);
		}
	}

	return 0;

err_max_rid:
	kfree(mxt->last_message);
err_msg_alloc:
	kfree(mxt->rid_map);
err_rid_map_alloc:
err_object_read:
	kfree(raw_ib_data);
err_ib_alloc:
	kfree(object_table);
err_object_table_alloc:
	return error;
}

//#define TS_PEN_IRQ_GPIO RK29_PIN0_PA2//61

static int Gpio_TS_request_irq(struct mxt_data *mxt)
{
	int rc;
    struct i2c_client *client = mxt->client;

	MXT1386_LOG(KERN_INFO "Gpio_TS_request_irq enter!\n", __func__);	
	MXT1386_LOG(KERN_INFO "%s: times 1 ts_irq value is %d\n", __func__, ts_irq);

	if (gpio_get_value(client->irq)) {
        MXT1386_LOG(KERN_INFO "%s: mxt1386 GPIO value is high\n", __func__);
    }
	else {
		MXT1386_LOG(KERN_INFO "%s: mxt1386 GPIO value is low\n", __func__);
	}

	MXT1386_LOG(KERN_INFO "%s: times 2 ts_irq value is %d\n", __func__, ts_irq);
	rc = request_irq(mxt->irq, mxt_irq_handler,
				IRQF_TRIGGER_LOW,//IRQF_TRIGGER_FALLING,  //
				"mxc_ts_i2c", mxt);
	if (rc) {
		pr_err("could not request irq\n");
		goto error_req_irq_fail;
	}

	MXT1386_LOG(KERN_INFO "Gpio_TS_request_irq ok!\n", __func__);
	return 0;

error_req_irq_fail:
error_irq_gpio_dir:
	gpio_free(client->irq);
error_irq_gpio_req:
	if (client->irq >= 0)
		gpio_free(client->irq);
	return rc;
}

static int mxt1386_platform_data_init(struct atmel_1386_platform_data *pdata)
{
	MXT1386_LOG(KERN_INFO "%s: mxt1386_platform_data_init enter!\n", __func__);

	pdata->max_x = CONFIG_ATMEL_MXT1386_MAX_X;
	pdata->max_y = CONFIG_ATMEL_MXT1386_MAX_Y;

	return 0;
}

static int __devinit mxt_probe(struct i2c_client *client,
			       const struct i2c_device_id *id)
{
	struct atmel_1386_platform_data *pdata = (struct atmel_1386_platform_data *)client->dev.platform_data;
	struct mxt_data          *mxt;
	struct input_dev         *input;
	u8 *id_data;
	int error;
	u8 buf[MXT_ACK_BUFFER_SIZE] = {0};
	u8 buf_size = MXT_MAKE_HIGH_CHG_SIZE_MIN;
    int index;
    
	MXT1386_LOG(" ---------------- mxt_probe start ------------ \n");
	MXT1386_LOG(KERN_INFO "%s: mxt_probe enter, debug level is = %d\n", __func__, debug);

	if (client == NULL) {
		MXT1386_LOG("maXTouch: client == NULL\n");
		return	-EINVAL;
	} else if (client->adapter == NULL) {
		MXT1386_LOG("maXTouch: client->adapter == NULL\n");
		return	-EINVAL;
	} else if (&client->dev == NULL) {
		MXT1386_LOG("maXTouch: client->dev == NULL\n");
		return	-EINVAL;
	} else if (&client->adapter->dev == NULL) {
		MXT1386_LOG("maXTouch: client->adapter->dev == NULL\n");
		return	-EINVAL;
	} else if (id == NULL) {
		MXT1386_LOG("maXTouch: id == NULL\n");
		return	-EINVAL;
	}

	mxt1386_platform_data_init(pdata);

	MXT1386_LOG(KERN_INFO "%s: maXTouch driver v. %s\n", __func__, DRIVER_VERSION);
	MXT1386_LOG(KERN_INFO "%s: \t \"%s\"\n", __func__, client->name);
	MXT1386_LOG(KERN_INFO "%s: \taddr:\t0x%04x\n", __func__, client->addr);
	MXT1386_LOG(KERN_INFO "%s: \tirq:\t%d\n", __func__, client->irq);
	MXT1386_LOG(KERN_INFO "%s: \tflags:\t0x%04x\n", __func__, client->flags);
	MXT1386_LOG(KERN_INFO "%s: \tadapter:\"%s\"\n", __func__, client->adapter->name);
	MXT1386_LOG(KERN_INFO "%s: \tdevice:\t\"%s\"\n", __func__, client->dev.init_name);

	if (!pdata) {
		MXT1386_LOG("TS i2c_probe no platform data\n");
	}
	else {
		MXT1386_LOG("TS i2c_probe platform data OK\n");
	}

	if (i2c_check_functionality(client->adapter, I2C_FUNC_I2C) == 0) {
		MXT1386_LOG("TS i2c_probe I2C_FUNC_I2C Failed\n");
	}
	else {
		MXT1386_LOG("TS i2c_probe I2C_FUNC_I2C OK\n");
	}
	
	if (pdata->init_platform_hw != NULL) {
		error = pdata->init_platform_hw(&(client->dev));
		if (error < 0) {
			MXT1386_LOG("TS i2c_probe power on Failed\n");
			return error;
		}
		else {
			MXT1386_LOG("TS i2c_probe power on OK\n");
		}
	}
	else {			/* ---------------- add by acgzx test ---------------- */
		MXT1386_LOG("TS init_platform_hw is NULL\n");
	}		

	/* Allocate structure - we need it to identify device */
	mxt = kzalloc(sizeof(struct mxt_data), GFP_KERNEL);
	if (mxt == NULL) {
		dev_err(&client->dev, "insufficient memory\n");
		error = -ENOMEM;
		goto err_mxt_alloc;
	}

	id_data = kmalloc(MXT_ID_BLOCK_SIZE, GFP_KERNEL);
	if (id_data == NULL) {
		dev_err(&client->dev, "insufficient memory\n");
		error = -ENOMEM;
		goto err_id_alloc;
	}

	input = input_allocate_device();
	if (!input) {
		dev_err(&client->dev, "error allocating input device\n");
		error = -ENOMEM;
		goto err_input_dev_alloc;
	}

	atmel_mxt = mxt;

	mxt->read_fail_counter = 0;
	mxt->message_counter   = 0;
	mxt->max_x_val         = pdata->max_x;
	mxt->max_y_val         = pdata->max_y;

	/* Get data that is defined in board specific code. */
	mxt->init_hw = pdata->init_platform_hw;
	mxt->exit_hw = pdata->exit_platform_hw;
	mxt->read_chg = pdata->read_chg;

	if (pdata->valid_interrupt != NULL)
		mxt->valid_interrupt = pdata->valid_interrupt;
	else
	{
		mxt->valid_interrupt = mxt_valid_interrupt_dummy;
		MXT1386_LOG("TS valid_interrupt is dummy\n");		/* ---------------- add by acgzx test ---------------- */
	}		

	if (debug >= DEBUG_TRACE)
		MXT1386_LOG("maXTouch driver identifying chip\n");

	if (mxt_identify(client, mxt, id_data) < 0) {
		dev_err(&client->dev, "Chip could not be identified\n");
		error = -ENODEV;
		goto err_identify;
	}
	/* Chip is valid and active. */
	if (debug >= DEBUG_TRACE)
		MXT1386_LOG("maXTouch driver allocating input device\n");

	mxt->client = client;
	mxt->input  = input;

	mutex_init(&mxt->debug_mutex);
	mutex_init(&mxt->msg_mutex);
	MXT1386_LOG("maXTouch driver creating device name\n");

	snprintf(
		mxt->phys_name,
		sizeof(mxt->phys_name),
		"%s/input0",
		dev_name(&client->dev)
	);
	input->name = "mxc_ts_i2c";	// "Atmel maXTouch Touchscreen controller";
	input->phys = mxt->phys_name;
	input->id.bustype = BUS_I2C;
	input->dev.parent = &client->dev;

	MXT1386_LOG("maXTouch name: \"%s\"\n", input->name);
	MXT1386_LOG("maXTouch phys: \"%s\"\n", input->phys);
	MXT1386_LOG("maXTouch driver setting abs parameters\n");

	MXT1386_LOG(KERN_INFO "%s: mxt->max_x_val = %d\n", __func__, mxt->max_x_val);
	MXT1386_LOG(KERN_INFO "%s: mxt->max_y_val = %d\n", __func__, mxt->max_y_val);
	
	/* Single touch */
	input_set_abs_params(input, ABS_X, 0, mxt->max_x_val, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, mxt->max_y_val, 0, 0);
	input_set_abs_params(input, ABS_PRESSURE, 0, MXT_MAX_REPORTED_PRESSURE,
			     0, 0);
	input_set_abs_params(input, ABS_TOOL_WIDTH, 0, MXT_MAX_REPORTED_WIDTH,
			     0, 0);

	/* Multitouch */
	input_set_abs_params(input, ABS_MT_POSITION_X, 0, mxt->max_x_val, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, mxt->max_y_val, 0, 0);
	input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0, MXT_MAX_TOUCH_SIZE,
			     0, 0);
	input_set_abs_params(input, ABS_MT_TRACKING_ID, 0, MXT_MAX_NUM_TOUCHES,
			     0, 0);
	
	__set_bit(EV_ABS, input->evbit);
	__set_bit(EV_SYN, input->evbit);
	__set_bit(EV_KEY, input->evbit);
	__set_bit(EV_MSC, input->evbit);

#if TOUCH_KEY_EN
	__set_bit(KEY_SEARCH, input->keybit);
	__set_bit(KEY_HOME, input->keybit);
	__set_bit(KEY_MENU, input->keybit);
	__set_bit(KEY_BACK, input->keybit);
#endif

	input->mscbit[0] = BIT_MASK(MSC_GESTURE);

	MXT1386_LOG("maXTouch driver setting client data\n");
	i2c_set_clientdata(client, mxt);
	MXT1386_LOG("maXTouch driver setting drv data\n");
	input_set_drvdata(input, mxt);
	MXT1386_LOG("maXTouch driver input register device\n");
	error = input_register_device(mxt->input);
	if (error < 0) {
		dev_err(&client->dev,
			"Failed to register input device\n");
		goto err_register_device;
	}

	error = mxt_read_object_table(client, mxt, id_data);
	if (error < 0)
		goto err_read_ot;

	/* Create debugfs entries. */
	mxt->debug_dir = debugfs_create_dir("maXTouch", NULL);
	if (mxt->debug_dir == ERR_PTR(-ENODEV)){
		/* debugfs is not enabled. */
		MXT1386_LOG("debugfs not enabled in kernel\n");
	} else if (mxt->debug_dir == NULL) {
		MXT1386_LOG("error creating debugfs dir\n");
	} else {
		MXT1386_LOG("created \"maXTouch\" debugfs dir\n");
		
		debugfs_create_file("deltas", S_IRUSR, mxt->debug_dir, mxt, 
				    &delta_fops);
		debugfs_create_file("refs", S_IRUSR, mxt->debug_dir, mxt,
				    &refs_fops);
	}

    /* Create character device nodes for reading & writing registers */
	mxt->mxt_class = class_create(THIS_MODULE, "maXTouch_memory");
	/* 2 numbers; one for memory and one for messages */
	error = alloc_chrdev_region(&mxt->dev_num, 0, 2, 
				    "maXTouch_memory");
	MXT1386_LOG("device number %d allocated!\n", MAJOR(mxt->dev_num));
	if (error){
		MXT1386_LOG("Error registering device\n");
	}
	cdev_init(&mxt->cdev, &mxt_memory_fops);
	cdev_init(&mxt->cdev_messages, &mxt_message_fops);
	
	MXT1386_LOG("cdev initialized\n");
	mxt->cdev.owner = THIS_MODULE;
	mxt->cdev_messages.owner = THIS_MODULE;
	
	error = cdev_add(&mxt->cdev, mxt->dev_num, 1);
	if (error){
		MXT1386_LOG("Bad cdev\n");
	}
	
	error = cdev_add(&mxt->cdev_messages, mxt->dev_num + 1, 1);
	if (error){
		MXT1386_LOG("Bad cdev\n");
	}
	
	MXT1386_LOG("cdev added\n");
	
	device_create(mxt->mxt_class, NULL, MKDEV(MAJOR(mxt->dev_num), 0), NULL,
		"maXTouch");

	device_create(mxt->mxt_class, NULL, MKDEV(MAJOR(mxt->dev_num), 1), NULL,
		"maXTouch_messages");

	mxt->msg_buffer_startp = 0;
	mxt->msg_buffer_endp = 0;

	/* Allocate the interrupt */
	MXT1386_LOG("maXTouch driver allocating interrupt...\n");
	mxt->irq = gpio_to_irq(client->irq);
    ts_irq = mxt->irq;
	mxt->valid_irq_counter = 0;
	mxt->invalid_irq_counter = 0;
	mxt->irq_counter = 0;

//WRITE_MT_CONFIG

#if WORKQ_RIGHTNOW
	MXT1386_LOG(KERN_INFO "%s: INIT_WORK set\n", __func__);
	mxt->queue = create_singlethread_workqueue("mxc_ts_handler"); /*创建一个单线程的工作队列*/
	INIT_WORK(&mxt->work, ts_thread_1386);	
#else
	MXT1386_LOG(KERN_INFO "%s: INIT_DELAYED_WORK set\n", __func__);
	mxt->queue = create_rt_workqueue("mxc_ts_handler");		/*创建一个实时的工作队列*/
	INIT_DELAYED_WORK(&mxt->dwork, ts_thread_1386);
#endif

/*--------------------------------------------------------------------------------*/
	/* Now the chip pull the irq pin to down waiting the host ack */
	if(gpio_get_value(client->irq)) {
		MXT1386_LOG(KERN_INFO "%s: TOUCH_INT_PIN test, value is high, error\n", __func__);
		goto err_irq;
	}
	else {
		MXT1386_LOG(KERN_INFO "%s: TOUCH_INT_PIN test, value is low, need host ack\n", __func__);
	}

	/* TP MCU 硬件复位后，此时INT引脚为低电平，主控需从T5寄存器读一段至少10bytes大小(不能小于此值)的数据反馈给MCU，此时INT引脚才拉高 acgzx 2011.03.30 */
	/* Read dummy message to make high CHG pin (Make CHG line high after the interrupts are enabled) */
	error = mxt_read_block(client, mxt->msg_proc_addr, buf_size, (u8 *)buf);	/* ---!!!!!!!!!!!!!!!!!!!!!!!!!!!!!--- */
	if (error < 0) {
		MXT1386_LOG(KERN_INFO "%s: host first ack msg err!\n", __func__);
	}
	else {
		MXT1386_LOG(KERN_INFO "%s: host first ack msg ok!\n", __func__);
	}
	mxt_buf_test(buf, buf_size);
    msleep(20);
	
	if(gpio_get_value(client->irq)) {
        MXT1386_LOG(KERN_INFO "%s: TOUCH_INT_PIN test, value is high, host acked chip successively\n", __func__);
    }
	else {
		MXT1386_LOG(KERN_INFO "%s: TOUCH_INT_PIN test, value is low, host acked chip fail\n", __func__);
		goto err_irq;
	}
/*--------------------------------------------------------------------------------*/
    atmel_1386_write_config(mxt);

	error = Gpio_TS_request_irq(mxt);
	if (error != 0) {
		MXT1386_LOG("TS i2c_probe request irq failed\n");
	       goto err_irq;
	}
	else {
		MXT1386_LOG("TS i2c_probe request irq ok\n");
	}

#ifdef CONFIG_PM
		device_init_wakeup(&client->dev, 0);
#endif

    if (debug > DEBUG_INFO)
		dev_info(&client->dev, "touchscreen, irq %d\n", mxt->irq);
		
	kfree(id_data);
	
	MXT1386_LOG(KERN_INFO "%s: mxt_probe init ok\n", __func__);
	MXT1386_LOG(" ---------------- mxt_probe end ------------ \n");
	return 0;

err_irq:
	kfree(mxt->rid_map);
	kfree(mxt->object_table);
	kfree(mxt->last_message);
err_read_ot:
err_register_device:
err_identify:
err_input_dev_alloc:
	kfree(id_data);
err_id_alloc:
	if (mxt->exit_hw != NULL)
		mxt->exit_hw(&(client->dev));
	kfree(mxt);
err_mxt_alloc:
	return error;
}

static int __devexit mxt_remove(struct i2c_client *client)
{
	struct mxt_data *mxt;

	MXT1386_LOG(KERN_INFO "%s: ----------- mxt_remove enter now -----------\n", __func__);

	mxt = i2c_get_clientdata(client);

	/* Remove debug dir entries */
	debugfs_remove_recursive(mxt->debug_dir);

	if (mxt != NULL) {
		
		if (mxt->exit_hw != NULL)
			mxt->exit_hw(&(client->dev));

		if (mxt->irq) {
			free_irq(mxt->irq, mxt);
		}

#if (!WORKQ_RIGHTNOW)
		if (cancel_delayed_work_sync(&mxt->dwork)) {
		/*
		 * Work was pending, therefore we need to enable
		 * IRQ here to balance the disable_irq() done in the
		 * interrupt handler.
		 */
		 	MXT1386_LOG(KERN_INFO "%s: cancel_delayed_work_sync set\n", __func__);
			enable_irq(mxt->irq);
		}
#endif

		unregister_chrdev_region(mxt->dev_num, 2);
		device_destroy(mxt->mxt_class, MKDEV(MAJOR(mxt->dev_num), 0));
		device_destroy(mxt->mxt_class, MKDEV(MAJOR(mxt->dev_num), 1));
		cdev_del(&mxt->cdev);
		cdev_del(&mxt->cdev_messages);
		cancel_delayed_work_sync(&mxt->dwork);
		input_unregister_device(mxt->input);
		class_destroy(mxt->mxt_class);
		debugfs_remove(mxt->debug_dir);

		kfree(mxt->rid_map);
		kfree(mxt->object_table);
		kfree(mxt->last_message);
	}
	kfree(mxt);

	i2c_set_clientdata(client, NULL);
	if (debug >= DEBUG_TRACE)
		dev_info(&client->dev, "Touchscreen unregistered\n");

	return 0;
}

#if defined(CONFIG_PM)
static int mxt_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct mxt_data *mxt = i2c_get_clientdata(client);

	MXT1386_LOG(KERN_INFO "%s: ----------- mxt_suspend enter now -----------\n", __func__);

#if 1
	if (device_may_wakeup(&client->dev)) {
		MXT1386_LOG(KERN_INFO "%s: mxt_suspend ok\n", __func__);
		enable_irq_wake(mxt->irq);
	} else {
		MXT1386_LOG(KERN_INFO "%s: mxt_suspend failed\n", __func__);
	}
#else
	disable_irq(mxt->irq);
	cancel_work_sync(&mxt->work);
	
	if (device_may_wakeup(&client->dev)) {
		printk(KERN_INFO "%s: mxt_suspend ok\n", __func__);
		enable_irq_wake(mxt->irq);
	} else {
		printk(KERN_INFO "%s: mxt_suspend failed\n", __func__);
	}
#endif

	return 0;
}

static int mxt_wakeup_controller(int gpio)
{
	int ret=0, i;

	gpio_free(gpio);

	if((ret = gpio_request(gpio, "mxc_irq_gpio"))!=0) {
		MXT1386_LOG(KERN_INFO "Failed to request GPIO for mxc_irq_gpio. Err:%d\n", ret);
		ret = -EFAULT;
	} else	{
		gpio_direction_output(gpio, 0);
		
		for(i=0; i<100; i++);
			gpio_direction_input(gpio);
		MXT1386_LOG(KERN_INFO "INT wakeup touch controller done\n");
	}
	
	return ret;
}


static int mxt_resume(struct i2c_client *client)
{
	struct mxt_data *mxt = i2c_get_clientdata(client);

	MXT1386_LOG(KERN_INFO "%s: ----------- mxt_resume enter now ----------- \n", __func__);

#if 1
	if (device_may_wakeup(&client->dev)) {
		MXT1386_LOG(KERN_INFO "%s: mxt_resume ok\n", __func__);
		disable_irq_wake(mxt->irq);
	} else {
		MXT1386_LOG(KERN_INFO "%s: mxt_resume failed\n", __func__);
	}
#else
	if (device_may_wakeup(&client->dev)) {
		printk(KERN_INFO "%s: mxt_resume ok\n", __func__);
		disable_irq_wake(mxt->irq);
	} else {
		printk(KERN_INFO "%s: mxt_resume failed\n", __func__);
	}
	mxt_wakeup_controller(irq_to_gpio(mxt->irq));
	enable_irq(mxt->irq);
#endif

	return 0;
}
#else
#define mxt_suspend NULL
#define mxt_resume NULL
#endif

static const struct i2c_device_id mxt_idtable[] = {
	{"mxc_ts_i2c", 0,},
	{ }
};

MODULE_DEVICE_TABLE(i2c, mxt_idtable);

static struct i2c_driver mxt_driver = {
	.driver = {
		.name	= "mxc_ts_i2c",
		.owner  = THIS_MODULE,
	},

	.id_table	= mxt_idtable,
	.probe		= mxt_probe,
	.remove		= __devexit_p(mxt_remove),
	.suspend	= mxt_suspend,
	.resume		= mxt_resume,

};

#if 0
static void __init mxt_init_async(void *unused, async_cookie_t cookie)
{
	int err;
	
	MXT1386_LOG(KERN_INFO "%s: ----------- mxt_init_async enter now ----------- \n", __func__);

	err = i2c_add_driver(&mxt_driver);
	if (err) {
		MXT1386_LOG("Adding maXTouch driver failed "
		       "(errno = %d)\n", err);
	} else {
		MXT1386_LOG(KERN_INFO "Successfully added driver %s\n", __func__,
		          mxt_driver.driver.name);
	}
}
static int __init mxt_init(void)
{
	MXT1386_LOG(KERN_INFO "%s: ----------- mxt_init enter now ----------- \n", __func__);
	
	async_schedule(mxt_init_async, NULL);
	return 0;
}
#else
static int __init mxt_init(void)
{
	int err;

	MXT1386_LOG(KERN_INFO "%s: ----------- mxt_init enter now ----------- \n", __func__);
	
	err = i2c_add_driver(&mxt_driver);
	if (err) {
		MXT1386_LOG("Adding maXTouch driver failed "
		       "(errno = %d)\n", err);
	} else {
		MXT1386_LOG(KERN_INFO "Successfully added driver %s\n", __func__,
		          mxt_driver.driver.name);
	}

	MXT1386_LOG(KERN_INFO "Adding maXTouch driver  \n");
	
	return err;
}
#endif

static void __exit mxt_cleanup(void)
{
	MXT1386_LOG(KERN_INFO "%s: ----------- mxt_cleanup enter now ----------- \n", __func__);
	i2c_del_driver(&mxt_driver);
}

module_init(mxt_init);
module_exit(mxt_cleanup);

MODULE_AUTHOR("Iiro Valkonen");
MODULE_DESCRIPTION("Driver for Atmel maXTouch Touchscreen Controller");
MODULE_LICENSE("GPL");

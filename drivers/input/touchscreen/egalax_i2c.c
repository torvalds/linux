/*
 *
 * Touch Screen I2C Driver for EETI Controller
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/freezer.h>
#include <linux/proc_fs.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <mach/regs-gpio.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/io.h>
#include <asm/system.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/kfifo.h>
#include <linux/version.h>
#include <linux/input.h>
#include <linux/irq.h>
#include <linux/timer.h>
#include <linux/proc_fs.h>
#include <plat/gpio-cfg.h>
#include <mach/regs-gpio.h>
#include <mach/gpio.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
static struct early_suspend egalax_early_suspend;
#endif

/* Global define to enable function */
#define _ENABLE_DBG_LEVEL

static int global_major; /* dynamic major by default */
static int global_minor;

#define MAX_I2C_LEN		10
#define FIFO_SIZE		PAGE_SIZE
#define MAX_SUPPORT_POINT	5
#define REPORTID_MOUSE		0x01
#define REPORTID_VENDOR		0x03
#define REPORTID_MTOUCH		0x04

/* ioctl command */
#define EGALAX_IOC_MAGIC	0x72
#define	EGALAX_IOCWAKEUP	_IO(EGALAX_IOC_MAGIC, 1)
#define EGALAX_IOC_MAXNR	1

/* running mode */
#define MODE_STOP	0
#define MODE_WORKING	1
#define MODE_IDLE	2
#define MODE_SUSPEND	3

#define EGALAX_MAX	2048
#define SCREEN_WIDTH	1280
#define SCREEN_HIGH	800

struct point_data {
	short Status;
	short X;
	short Y;
};

struct egalax_i2c_platform_data {
	unsigned int gpio_int;
	unsigned int gpio_en;
	unsigned int gpio_rst;
};

struct _egalax_i2c {
	struct egalax_i2c_platform_data *pdata;
	struct workqueue_struct *ktouch_wq;
	struct work_struct work_irq;
	struct work_struct work_idle;
	struct mutex mutex_wq;
	struct i2c_client *client;
	unsigned char work_state;
	unsigned char skip_packet;
	unsigned char downCnt;
	struct timer_list idle_timer;
};

struct egalax_char_dev {
	int OpenCnts;
	struct cdev cdev;
	struct kfifo DataKFiFo;
	unsigned char *pFiFoBuf;
	spinlock_t FiFoLock;
	struct semaphore sem;
	wait_queue_head_t fifo_inq;
};

static struct _egalax_i2c *p_egalax_i2c_dev;
static struct egalax_char_dev *p_char_dev;
static atomic_t egalax_char_available = ATOMIC_INIT(1);
static atomic_t wait_command_ack = ATOMIC_INIT(0);
static struct class *egalax_class;
static struct input_dev *input_dev;
static struct point_data PointBuf[MAX_SUPPORT_POINT];

struct mutex i2c_lock;

#define DBG_MODULE	0x00000001
#define DBG_CDEV	0x00000002
#define DBG_PROC	0x00000004
#define DBG_POINT	0x00000008
#define DBG_INT		0x00000010
#define DBG_I2C		0x00000020
#define DBG_SUSP	0x00000040
#define DBG_INPUT	0x00000080
#define DBG_CONST	0x00000100
#define DBG_IDLE	0x00000200
#define DBG_WAKEUP	0x00000400
#define DBG_BUTTON	0x00000800
static unsigned int DbgLevel; /* DBG_INT|DBG_MODULE|DBG_SUSP|DBG_WAKEUP */

#ifdef _ENABLE_DBG_LEVEL
	#define PROC_FS_NAME	"egalax_dbg"
	#define PROC_FS_MAX_LEN	8
	static struct proc_dir_entry *dbgProcFile;
#endif

#define EGALAX_DBG(level, fmt, args...) { if ((level&DbgLevel) > 0) \
			printk(KERN_INFO "[egalax_i2c]: " fmt, ## args); }
#define IDLE_INTERVAL HZ/20 	/* 50ms */

static int sendLoopback(struct i2c_client *client)
{
	u8 cmdbuf[MAX_I2C_LEN] = {0x03, 0x03, 0x0A, 0x01, 0x41, 0, 0, 0, 0, 0};
	int ret;
	ret = 0;
	mutex_lock(&i2c_lock);
	if (i2c_master_send(client, cmdbuf, MAX_I2C_LEN) != MAX_I2C_LEN) {
		ret = -1;
		printk("TS-SendLoopback (I2C Write)Error\n");
	}
	mutex_unlock(&i2c_lock);
	return ret;
}

static int wakeup_controller(int irq)
{
	int ret = 0;
	int gpio = p_egalax_i2c_dev->pdata->gpio_int;

	if (gpio_get_value(gpio)) {
		gpio_direction_output(gpio, 0);
		barrier();
		mdelay(5);
	}

	gpio_direction_output(gpio, 1);
	gpio_direction_input(gpio);
	EGALAX_DBG(DBG_WAKEUP, " INT wakeup touch controller done\n");

	return ret;
}

static int egalax_cdev_open(struct inode *inode, struct file *filp)
{
	struct egalax_char_dev *cdev;

	cdev = container_of(inode->i_cdev, struct egalax_char_dev, cdev);
	if (cdev == NULL) {
		EGALAX_DBG(DBG_CDEV, "No such char device node\n");
		return -ENODEV;
	}

	if (!atomic_dec_and_test(&egalax_char_available)) {
		atomic_inc(&egalax_char_available);
		return -EBUSY; /* already open */
	}

	cdev->OpenCnts++;
	filp->private_data = cdev;/* Used by the read and write metheds */

	/* check and wakeup controller if necessary */
	del_timer_sync(&p_egalax_i2c_dev->idle_timer);
	cancel_work_sync(&p_egalax_i2c_dev->work_idle);
	if (p_egalax_i2c_dev->work_state == MODE_IDLE)
		wakeup_controller(p_egalax_i2c_dev->client->irq);

	EGALAX_DBG(DBG_CDEV, " CDev open done!\n");
	try_module_get(THIS_MODULE);
	return 0;
}

static int egalax_cdev_release(struct inode *inode, struct file *filp)
{
	struct egalax_char_dev *cdev;/* device information */

	cdev = container_of(inode->i_cdev, struct egalax_char_dev, cdev);
	if (cdev == NULL) {
		EGALAX_DBG(DBG_CDEV, "No such char device node\n");
		return -ENODEV;
	}

	atomic_inc(&egalax_char_available); /* release the device */

	filp->private_data = NULL;
	cdev->OpenCnts--;

	kfifo_reset(&cdev->DataKFiFo);

	mod_timer(&p_egalax_i2c_dev->idle_timer, jiffies+IDLE_INTERVAL);

	EGALAX_DBG(DBG_CDEV, "CDev release done!\n");
	module_put(THIS_MODULE);
	return 0;
}

#define MAX_READ_BUF_LEN	50
static char fifo_read_buf[MAX_READ_BUF_LEN];
static ssize_t egalax_cdev_read(struct file *file, char __user *buf,
						size_t count, loff_t *offset)
{
	int read_cnt, ret, fifoLen;
	struct egalax_char_dev *cdev = file->private_data;

	if (down_interruptible(&cdev->sem))
		return -ERESTARTSYS;

	fifoLen = kfifo_len(&cdev->DataKFiFo);

	while (fifoLen < 1) {	/* nothing to read */
		up(&cdev->sem);	/* release the lock */
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(cdev->fifo_inq,
					kfifo_len(&cdev->DataKFiFo) > 0)) {
			return -ERESTARTSYS;
		}

		if (down_interruptible(&cdev->sem))
			return -ERESTARTSYS;
	}

	if (count > MAX_READ_BUF_LEN)
		count = MAX_READ_BUF_LEN;

	EGALAX_DBG(DBG_CDEV, " \"%s\" reading fifo data\n", current->comm);
	read_cnt = kfifo_out_locked(&cdev->DataKFiFo, fifo_read_buf,
							count, &cdev->FiFoLock);

	ret = copy_to_user(buf, fifo_read_buf, read_cnt) ? -EFAULT : read_cnt;

	up(&cdev->sem);

	return ret;
}

static ssize_t egalax_cdev_write(struct file *file, const char __user *buf,
						size_t count, loff_t *offset)
{
	struct egalax_char_dev *cdev = file->private_data;
	int ret = 0;
	char *tmp;

	if (down_interruptible(&cdev->sem))
		return -ERESTARTSYS;

	if (count > MAX_I2C_LEN)
		count = MAX_I2C_LEN;

	tmp = kmalloc(count, GFP_KERNEL);
	if (tmp == NULL) {
		up(&cdev->sem);
		return -ENOMEM;
	}

	if (copy_from_user(tmp, buf, count)) {
		up(&cdev->sem);
		kfree(tmp);
		return -EFAULT;
	}

	ret = i2c_master_send(p_egalax_i2c_dev->client, tmp, count);

	up(&cdev->sem);
	EGALAX_DBG(DBG_CDEV, " I2C writing %zu bytes.\n", count);
	kfree(tmp);

	return ret;
}

#ifdef _ENABLE_DBG_LEVEL
static int egalax_proc_read(char *buffer, char **buffer_location, off_t offset,
				int buffer_length, int *eof, void *data)
{
	int ret;

	EGALAX_DBG(DBG_PROC, " \"%s\" call proc_read\n", current->comm);

	if (offset > 0) /* we have finished to read, return 0 */
		ret  = 0;
	else
		ret = sprintf(buffer, "Debug Level: 0x%08X\n", DbgLevel);

	return ret;
}

static int egalax_proc_write(struct file *file, const char *buffer,
					unsigned long count, void *data)
{
	char procfs_buffer_size = 0;
	int i;
	unsigned char procfs_buf[PROC_FS_MAX_LEN] = {0};

	EGALAX_DBG(DBG_PROC, " \"%s\" call proc_write\n", current->comm);

	procfs_buffer_size = count;
	if (procfs_buffer_size > PROC_FS_MAX_LEN)
		procfs_buffer_size = PROC_FS_MAX_LEN+1;

	if (copy_from_user(procfs_buf, buffer, procfs_buffer_size)) {
		EGALAX_DBG(DBG_PROC, " proc_write faied at copy_from_user\n");
		return -EFAULT;
	}

	DbgLevel = 0;
	for (i = 0 ; i < procfs_buffer_size-1; i++)	{
		if (procfs_buf[i] >= '0' && procfs_buf[i] <= '9')
			DbgLevel |= (procfs_buf[i] - '0');
		else if (procfs_buf[i] >= 'A' && procfs_buf[i] <= 'F')
			DbgLevel |= (procfs_buf[i] - 'A' + 10);
		else if (procfs_buf[i] >= 'a' && procfs_buf[i] <= 'f')
			DbgLevel |= (procfs_buf[i] - 'a' + 10);

		if (i != procfs_buffer_size - 2)
			DbgLevel <<= 4;
	}

	DbgLevel = DbgLevel&0xFFFFFFFF;

	EGALAX_DBG(DBG_PROC, " Switch Debug Level to 0x%08X\n", DbgLevel);

	return count; /* procfs_buffer_size */
}
#endif /* #ifdef _ENABLE_DBG_LEVEL */

static long egalax_cdev_ioctl(struct file *filp, unsigned int cmd,
							unsigned long args)
{
	int ret = 0;

	if (_IOC_TYPE(cmd) != EGALAX_IOC_MAGIC)
		return -ENOTTY;
	if (_IOC_NR(cmd) > EGALAX_IOC_MAXNR)
		return -ENOTTY;

	if (_IOC_DIR(cmd) & _IOC_READ)
		ret = !access_ok(VERIFY_WRITE, (void __user *)args,
							_IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		ret = !access_ok(VERIFY_READ, (void __user *)args,
							_IOC_SIZE(cmd));

	if (ret)
		return -EFAULT;

	EGALAX_DBG(DBG_CDEV, "Handle device ioctl command\n");
	switch (cmd) {
	case EGALAX_IOCWAKEUP:
		ret = wakeup_controller(p_egalax_i2c_dev->client->irq);
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

static unsigned int egalax_cdev_poll(struct file *filp,
						struct poll_table_struct *wait)
{
	struct egalax_char_dev *cdev = filp->private_data;
	unsigned int mask = 0;
	int fifoLen;

	down(&cdev->sem);
	poll_wait(filp, &cdev->fifo_inq,  wait);

	fifoLen = kfifo_len(&cdev->DataKFiFo);

	if (fifoLen > 0)
		mask |= POLLIN | POLLRDNORM;	/* readable */
	if ((FIFO_SIZE - fifoLen) > MAX_I2C_LEN)
		mask |= POLLOUT | POLLWRNORM;	/* writable */

	up(&cdev->sem);
	return mask;
}

static int LastUpdateID;
static void ProcessReport(unsigned char *buf, struct _egalax_i2c *p_egalax_i2c)
{
	int i, cnt_down = 0, cnt_up = 0;
	short X, Y, ContactID, Status;
	bool bNeedReport = false;
	int skip_point = 0;

	Status = buf[1]&0x01;
	ContactID = (buf[1]&0x7C)>>2;
	X = ((buf[3]<<8) + buf[2])>>4;
	X = X * SCREEN_WIDTH / EGALAX_MAX;
	Y = ((buf[5]<<8) + buf[4])>>4;
	Y = Y * SCREEN_HIGH / EGALAX_MAX;

	if (!(ContactID >= 0 && ContactID < MAX_SUPPORT_POINT))	{
		EGALAX_DBG(DBG_POINT, "Get I2C Point data error [%02X][%02X]\
			[%02X][%02X][%02X][%02X]\n", buf[0], buf[1],
			buf[2], buf[3], buf[4], buf[5]);
		return;
	}

#ifdef CONFIG_TC4_PORTRAIT_MODE
	PointBuf[ContactID].X = EGALAX_MAX-Y;
	PointBuf[ContactID].Y = X;
#else
	PointBuf[ContactID].X = X;
	PointBuf[ContactID].Y = Y;
#endif
	if (PointBuf[ContactID].Status != Status) {
		if (Status)
			p_egalax_i2c->downCnt++;
		else if (PointBuf[ContactID].Status > 0)
			p_egalax_i2c->downCnt--;

		PointBuf[ContactID].Status = Status;
		bNeedReport = true;
	}

	/* Send point report */
	if ((bNeedReport || (ContactID <= LastUpdateID)) && (skip_point == 0)) {
		for (i = 0; i < MAX_SUPPORT_POINT; i++) {
			if (PointBuf[i].Status > 0) {
				input_report_abs(input_dev,
					ABS_MT_TRACKING_ID, i);
				input_report_abs(input_dev,
					ABS_MT_TOUCH_MAJOR, PointBuf[i].Status);
				input_report_abs(input_dev,
					ABS_MT_WIDTH_MAJOR, 0);
				input_report_abs(input_dev,
					ABS_MT_POSITION_X, PointBuf[ContactID].X);
				input_report_abs(input_dev,
					ABS_MT_POSITION_Y, PointBuf[ContactID].Y);
				input_mt_sync(input_dev);
				cnt_down++;
			} else if (PointBuf[i].Status == 0) {
				PointBuf[i].Status--;
				cnt_up++;
				input_mt_sync(input_dev);
			}
		}
		input_sync(input_dev);
		EGALAX_DBG(DBG_POINT, " Input sync point data done! (Down:%d Up:%d)\n",
								cnt_down, cnt_up);
	}

	LastUpdateID = ContactID;
}

static struct input_dev *allocate_Input_Dev(void)
{
	int ret;
	struct input_dev *pInputDev;

	pInputDev = input_allocate_device();
	if (pInputDev == NULL) {
		EGALAX_DBG(DBG_MODULE, "Failed to allocate input device\n");
		return NULL;
	}

	pInputDev->name = "egalax_i2c";
	pInputDev->phys = "I2C";
	pInputDev->id.bustype = BUS_I2C;
	pInputDev->id.vendor = 0x0EEF;
	pInputDev->id.product = 0x0020;
	pInputDev->id.version = 0x0001;

	set_bit(EV_ABS, pInputDev->evbit);
	input_set_abs_params(pInputDev, ABS_MT_POSITION_X,
					0, SCREEN_WIDTH, 0, 0);
	input_set_abs_params(pInputDev, ABS_MT_POSITION_Y,
					0, SCREEN_HIGH, 0, 0);
	input_set_abs_params(pInputDev, ABS_MT_TOUCH_MAJOR,
					0, 255, 0, 0);
	input_set_abs_params(pInputDev, ABS_MT_WIDTH_MAJOR,
					0, 255, 0, 0);
	input_set_abs_params(pInputDev, ABS_MT_TRACKING_ID,
					0, MAX_SUPPORT_POINT, 0, 0);

	ret = input_register_device(pInputDev);
	if (ret) {
		EGALAX_DBG(DBG_MODULE, "Unable to register input device.\n");
		input_free_device(pInputDev);
		pInputDev = NULL;
	}

	return pInputDev;
}

static int egalax_i2c_measure(struct _egalax_i2c *egalax_i2c)
{
	struct i2c_client *client = egalax_i2c->client;
	u8 x_buf[MAX_I2C_LEN];
	int count, loop = 3;

	EGALAX_DBG(DBG_INT, "egalax_i2c_measure\n");

	do {
		mutex_lock(&i2c_lock);
		count = i2c_master_recv(client, x_buf, MAX_I2C_LEN);
		mutex_unlock(&i2c_lock);
	} while (count == EAGAIN && --loop);

	if (count < 0 || (x_buf[0] != REPORTID_VENDOR && x_buf[0]
						!= REPORTID_MTOUCH)) {
		EGALAX_DBG(DBG_I2C, "I2C read error data\
				with Len=%d hedaer=%d\n", count, x_buf[0]);
		return -1;
	}

	EGALAX_DBG(DBG_I2C, " I2C read data with Len=%d\n", count);
	if (x_buf[0] == REPORTID_VENDOR) {
		atomic_set(&wait_command_ack, 1);
		EGALAX_DBG(DBG_I2C, " I2C get vendor command packet\n");
	}

	if (egalax_i2c->skip_packet > 0)
		return count;

	/* check buffer len & header */
	if (count == MAX_I2C_LEN && x_buf[0] == REPORTID_MTOUCH) {
		ProcessReport(x_buf, egalax_i2c);
		return count;
	}

	/* If someone reading now! put the data into the buffer! */
	if (count > 0 && p_char_dev->OpenCnts > 0) {
		kfifo_in_locked(&p_char_dev->DataKFiFo, x_buf,
					count, &p_char_dev->FiFoLock);
		wake_up_interruptible(&p_char_dev->fifo_inq);
	}

	return count;
}

static void egalax_i2c_wq_irq(struct work_struct *work)
{
	struct _egalax_i2c *egalax_i2c =
			container_of(work, struct _egalax_i2c, work_irq);
	struct i2c_client *client = egalax_i2c->client;
	int gpio = egalax_i2c->pdata->gpio_int;

	EGALAX_DBG(DBG_INT, " egalax_i2c_wq run\n");

	mutex_lock(&egalax_i2c->mutex_wq);
	/*continue recv data*/
	while (!gpio_get_value(gpio)) {
		egalax_i2c_measure(egalax_i2c);
		schedule();
	}

	if (egalax_i2c->skip_packet > 0)
		egalax_i2c->skip_packet = 0;

	if (p_char_dev->OpenCnts <= 0 && egalax_i2c->work_state == MODE_WORKING)
		mod_timer(&egalax_i2c->idle_timer, jiffies+IDLE_INTERVAL);

	mutex_unlock(&egalax_i2c->mutex_wq);

	enable_irq(client->irq);

	EGALAX_DBG(DBG_INT, " egalax_i2c_wq leave\n");
}

static irqreturn_t egalax_i2c_interrupt(int irq, void *dev_id)
{
	struct _egalax_i2c *egalax_i2c = (struct _egalax_i2c *)dev_id;

	EGALAX_DBG(DBG_INT, " INT with irq:%d\n", irq);

	del_timer(&egalax_i2c->idle_timer);
	if (egalax_i2c->work_state == MODE_IDLE)
		egalax_i2c->work_state = MODE_WORKING;

	disable_irq_nosync(irq);
	queue_work(egalax_i2c->ktouch_wq, &egalax_i2c->work_irq);

	return IRQ_HANDLED;
}

static void egalax_i2c_wq_idle(struct work_struct *work)
{
	struct _egalax_i2c *egalax_i2c =
			container_of(work, struct _egalax_i2c, work_idle);
	unsigned char buf[] = {0x03, 0x06, 0x0A, 0x04, 0x36,
					0x3F, 0x01, 0x00, 0, 0};
	int ret = 0;

	if (egalax_i2c->work_state == MODE_WORKING) {
		mutex_lock(&i2c_lock);
		ret = i2c_master_send(egalax_i2c->client, buf, MAX_I2C_LEN);
		mutex_unlock(&i2c_lock);
		if (ret == MAX_I2C_LEN) {
			egalax_i2c->work_state = MODE_IDLE;
			EGALAX_DBG(DBG_IDLE, " Set controller to idle mode\n");
		} else
			EGALAX_DBG(DBG_IDLE, " Try to set controller\
						to idle failed:%d\n", ret);
	}
}

static void egalax_idle_timer_routine(unsigned long data)
{
	struct _egalax_i2c *egalax_i2c = (struct _egalax_i2c *)data;

	queue_work(egalax_i2c->ktouch_wq, &egalax_i2c->work_idle);
}



#ifdef CONFIG_HAS_EARLYSUSPEND
static void egalax_i2c_early_suspend(struct early_suspend *handler)
{
	mod_timer(&p_egalax_i2c_dev->idle_timer, jiffies);

	return;
}

#endif // #ifdef CONFIG_HAS_EARLYSUSPEND

static int __devinit egalax_i2c_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int i, ret = 0;
	int gpio;

	EGALAX_DBG(DBG_MODULE, " Start probe\n");

	p_egalax_i2c_dev = kzalloc(sizeof(struct _egalax_i2c), GFP_KERNEL);
	if (!p_egalax_i2c_dev) {
		EGALAX_DBG(DBG_MODULE, "Request memory failed\n");
		ret = -ENOMEM;
		goto fail1;
	}

	p_egalax_i2c_dev->pdata = kmalloc(sizeof(*(p_egalax_i2c_dev->pdata)),
								GFP_KERNEL);
	if (NULL == p_egalax_i2c_dev->pdata) {
		dev_err(&client->dev, "fail to allocate mem for pdata\n");
		goto fail1_1;
	}

	memcpy(p_egalax_i2c_dev->pdata, client->dev.platform_data,
					sizeof(*(p_egalax_i2c_dev->pdata)));

	gpio = p_egalax_i2c_dev->pdata->gpio_int;

	input_dev = allocate_Input_Dev();
	if (input_dev == NULL) {
		EGALAX_DBG(DBG_MODULE, " allocate_Input_Dev failed\n");
		ret = -EINVAL;
		goto fail2;
	}

	EGALAX_DBG(DBG_MODULE, " Register input device done\n");

	for (i = 0; i < MAX_SUPPORT_POINT; i++) {
		PointBuf[i].Status = -1;
		PointBuf[i].X = PointBuf[i].Y = 0;
	}

	p_egalax_i2c_dev->client = client;
	mutex_init(&p_egalax_i2c_dev->mutex_wq);
	mutex_init(&i2c_lock);	/* cuiwenpin:i2c read write protection */

	p_egalax_i2c_dev->ktouch_wq =
			create_singlethread_workqueue("egalax_touch_wq");
	INIT_WORK(&p_egalax_i2c_dev->work_irq, egalax_i2c_wq_irq);
	INIT_WORK(&p_egalax_i2c_dev->work_idle, egalax_i2c_wq_idle);

	i2c_set_clientdata(client, p_egalax_i2c_dev);

	sendLoopback(client);

	if (gpio_get_value(gpio))
		p_egalax_i2c_dev->skip_packet = 0;
	else
		p_egalax_i2c_dev->skip_packet = 1;

	p_egalax_i2c_dev->work_state = MODE_WORKING;

	/* setup timer */
	setup_timer(&p_egalax_i2c_dev->idle_timer, egalax_idle_timer_routine,
					(unsigned long)p_egalax_i2c_dev);
	mod_timer(&p_egalax_i2c_dev->idle_timer, jiffies);

	ret = request_irq(client->irq, egalax_i2c_interrupt,
				IRQF_DISABLED | IRQF_TRIGGER_FALLING,
				client->name, p_egalax_i2c_dev);
	if (ret) {
		EGALAX_DBG(DBG_MODULE, "Request irq(%d) failed\n", client->irq);
		goto fail3;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	egalax_early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
	egalax_early_suspend.suspend = egalax_i2c_early_suspend;
	egalax_early_suspend.resume = NULL;
	register_early_suspend(&egalax_early_suspend);
	EGALAX_DBG(DBG_MODULE, " Register early_suspend done\n");
#endif

	EGALAX_DBG(DBG_MODULE, "Request irq(%d) gpio(%d) with result:%d\n",
							client->irq, gpio, ret);


	EGALAX_DBG(DBG_MODULE, " I2C probe done\n");
	return 0;

fail3:
	i2c_set_clientdata(client, NULL);
	destroy_workqueue(p_egalax_i2c_dev->ktouch_wq);
	free_irq(client->irq, p_egalax_i2c_dev);
	input_unregister_device(input_dev);
	input_dev = NULL;
fail2:
	kfree(p_egalax_i2c_dev->pdata);
fail1_1:
	kfree(p_egalax_i2c_dev);
fail1:
	p_egalax_i2c_dev = NULL;

	EGALAX_DBG(DBG_MODULE, " I2C probe failed\n");
	return ret;
}

static int __devexit egalax_i2c_remove(struct i2c_client *client)
{
	struct _egalax_i2c *egalax_i2c = i2c_get_clientdata(client);

	egalax_i2c->work_state = MODE_STOP;

	cancel_work_sync(&egalax_i2c->work_irq);
	del_timer_sync(&egalax_i2c->idle_timer);
	cancel_work_sync(&egalax_i2c->work_idle);

	if (client->irq) {
		disable_irq(client->irq);
		free_irq(client->irq, egalax_i2c);
	}

	if (egalax_i2c->ktouch_wq)
		destroy_workqueue(egalax_i2c->ktouch_wq);

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&egalax_early_suspend);
#endif

	if (input_dev) {
		EGALAX_DBG(DBG_MODULE,  " Unregister input device\n");
		input_unregister_device(input_dev);
		input_dev = NULL;
	}

	i2c_set_clientdata(client, NULL);
	kfree(egalax_i2c->pdata);
	kfree(egalax_i2c);
	p_egalax_i2c_dev = NULL;

	return 0;
}

static const struct i2c_device_id egalax_i2c_idtable[] = {
	{ "egalax_i2c", 0 },
	{}
};

MODULE_DEVICE_TABLE(i2c, egalax_i2c_idtable);

static struct i2c_driver egalax_i2c_driver = {
	.driver = {
		.name	= "egalax_i2c",
	},
	.id_table	= egalax_i2c_idtable,
	.probe		= egalax_i2c_probe,
	.remove		= __devexit_p(egalax_i2c_remove),
};

static const struct file_operations egalax_cdev_fops = {
	.owner		= THIS_MODULE,
	.read		= egalax_cdev_read,
	.write		= egalax_cdev_write,
	.unlocked_ioctl	= egalax_cdev_ioctl,
	.poll		= egalax_cdev_poll,
	.open		= egalax_cdev_open,
	.release	= egalax_cdev_release,
};

static void egalax_i2c_ts_exit(void)
{
	dev_t devno = MKDEV(global_major, global_minor);

	if (p_char_dev) {
		EGALAX_DBG(DBG_MODULE, "Unregister character device\n");
		kfree(p_char_dev->pFiFoBuf);

		cdev_del(&p_char_dev->cdev);
		kfree(p_char_dev);
		p_char_dev = NULL;
	}

	unregister_chrdev_region(devno, 1);

	if (!IS_ERR(egalax_class)) {
		device_destroy(egalax_class, devno);
		class_destroy(egalax_class);
	}

	i2c_del_driver(&egalax_i2c_driver);

#ifdef _ENABLE_DBG_LEVEL
	remove_proc_entry(PROC_FS_NAME, NULL);
#endif

	EGALAX_DBG(DBG_MODULE, " Exit driver done!\n");
}

static struct egalax_char_dev *setup_chardev(dev_t dev)
{
	struct egalax_char_dev *pCharDev;
	int result;

	pCharDev = kmalloc(1 * sizeof(struct egalax_char_dev), GFP_KERNEL);
	if (!pCharDev)
		goto fail_cdev;

	memset(pCharDev, 0, sizeof(struct egalax_char_dev));

	spin_lock_init(&pCharDev->FiFoLock);
	pCharDev->pFiFoBuf = kmalloc(sizeof(unsigned char)*FIFO_SIZE,
								GFP_KERNEL);
	if (!pCharDev->pFiFoBuf)
		goto fail_fifobuf;
	memset(pCharDev->pFiFoBuf, 0, sizeof(unsigned char)*FIFO_SIZE);

	kfifo_init(&pCharDev->DataKFiFo, pCharDev->pFiFoBuf, FIFO_SIZE);
	if (!kfifo_initialized(&pCharDev->DataKFiFo))
		goto fail_kfifo;

	pCharDev->OpenCnts = 0;
	cdev_init(&pCharDev->cdev, &egalax_cdev_fops);
	pCharDev->cdev.owner = THIS_MODULE;
	sema_init(&pCharDev->sem, 1);
	init_waitqueue_head(&pCharDev->fifo_inq);

	result = cdev_add(&pCharDev->cdev, dev, 1);
	if (result) {
		EGALAX_DBG(DBG_MODULE, " Failed at cdev added\n");
		goto fail_kfifo;
	}

	return pCharDev;

fail_kfifo:
	kfree(pCharDev->pFiFoBuf);
fail_fifobuf:
	kfree(pCharDev);
fail_cdev:
	return NULL;
}

static int egalax_i2c_ts_init(void)
{
	int result;
	dev_t devno = 0;

	/* Asking for a dynamic major unless directed otherwise at load time. */
	if (global_major) {
		devno = MKDEV(global_major, global_minor);
		result = register_chrdev_region(devno, 1, "egalax_i2c");
	} else {
		result = alloc_chrdev_region(&devno, global_minor,
							1, "egalax_i2c");
		global_major = MAJOR(devno);
	}

	if (result < 0) {
		EGALAX_DBG(DBG_MODULE, " Cdev can't get major number\n");
		return 0;
	}

	/* allocate the character device */
	p_char_dev = setup_chardev(devno);
	if (!p_char_dev) {
		result = -ENOMEM;
		goto fail;
	}

	egalax_class = class_create(THIS_MODULE, "egalax_i2c");
	if (IS_ERR(egalax_class)) {
		EGALAX_DBG(DBG_MODULE, " Failed in creating class.\n");
		result = -EFAULT;
		goto fail;
	}

	device_create(egalax_class, NULL, devno, NULL, "egalax_i2c");
	EGALAX_DBG(DBG_MODULE, "Register egalax_i2c cdev, major: %d\n",
								global_major);

#ifdef _ENABLE_DBG_LEVEL
	dbgProcFile = create_proc_entry(PROC_FS_NAME, 0666, NULL);
	if (dbgProcFile == NULL) {
		remove_proc_entry(PROC_FS_NAME, NULL);
		EGALAX_DBG(DBG_MODULE, "Could not initialize /proc/%s\n",
								PROC_FS_NAME);
	} else {
		dbgProcFile->read_proc = egalax_proc_read;
		dbgProcFile->write_proc = egalax_proc_write;
		EGALAX_DBG(DBG_MODULE, " /proc/%s created\n", PROC_FS_NAME);
	}
#endif /* #ifdef _ENABLE_DBG_LEVEL */

	EGALAX_DBG(DBG_MODULE, " Driver init done!\n");
	return i2c_add_driver(&egalax_i2c_driver);

fail:
	egalax_i2c_ts_exit();
	return result;
}

module_init(egalax_i2c_ts_init);
module_exit(egalax_i2c_ts_exit);

MODULE_AUTHOR("EETI <touch_fae@eeti.com>");
MODULE_DESCRIPTION("egalax touch screen i2c driver");
MODULE_LICENSE("GPL");

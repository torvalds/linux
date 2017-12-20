/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/*-----------linux system header files----------------*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/poll.h>

/*#include <mach/eint.h>*/
/*-----------driver own header files----------------*/
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif
#ifdef DFT_TAG
#undef DFT_TAG
#endif
#define DFT_TAG "MTK-BTIF"

#define BTIF_CDEV_SUPPORT 1

#include "btif_pub.h"
#include "btif_dma_pub.h"
#include "mtk_btif_exp.h"
#include "mtk_btif.h"

/*-----------static function declearation----------------*/
static int mtk_btif_probe(struct platform_device *pdev);
static int mtk_btif_remove(struct platform_device *pdev);
static int mtk_btif_suspend(struct platform_device *pdev, pm_message_t state);
static int mtk_btif_resume(struct platform_device *pdev);
static int mtk_btif_drv_resume(struct device *dev);
static int mtk_btif_drv_suspend(struct device *pdev);

static int mtk_btif_restore_noirq(struct device *device);
static int btif_file_open(struct inode *pinode, struct file *pfile);
static int btif_file_release(struct inode *pinode, struct file *pfile);
static ssize_t btif_file_read(struct file *pfile,
			      char __user *buf, size_t count, loff_t *f_ops);
static unsigned int btif_poll(struct file *filp, poll_table *wait);
static int _btif_irq_reg(P_MTK_BTIF_IRQ_STR p_irq,
		  mtk_btif_irq_handler irq_handler, void *data);
static int _btif_irq_free(P_MTK_BTIF_IRQ_STR p_irq, void *data);
static int _btif_irq_ctrl(P_MTK_BTIF_IRQ_STR p_irq, bool en);
static int _btif_irq_ctrl_sync(P_MTK_BTIF_IRQ_STR p_irq, bool en);
static irqreturn_t btif_irq_handler(int irq, void *data);
static unsigned int btif_pio_rx_data_receiver(P_MTK_BTIF_INFO_STR p_btif_info,
				       unsigned char *p_buf,
				       unsigned int buf_len);

static irqreturn_t btif_tx_dma_irq_handler(int irq, void *data);
static irqreturn_t btif_rx_dma_irq_handler(int irq, void *data);

static unsigned int btif_dma_rx_data_receiver(P_MTK_DMA_INFO_STR p_dma_info,
				       unsigned char *p_buf,
				       unsigned int buf_len);
static int _btif_controller_tx_setup(p_mtk_btif p_btif);
static int _btif_controller_tx_free(p_mtk_btif p_btif);
static int _btif_controller_rx_setup(p_mtk_btif p_btif);
static int _btif_controller_rx_free(p_mtk_btif p_btif);
static int _btif_tx_pio_setup(p_mtk_btif p_btif);
static int _btif_rx_pio_setup(p_mtk_btif p_btif);
static int _btif_rx_dma_setup(p_mtk_btif p_btif);
static int _btif_rx_dma_free(p_mtk_btif p_btif);
static int _btif_tx_dma_setup(p_mtk_btif p_btif);
static int _btif_tx_dma_free(p_mtk_btif p_btif);
static int _btif_controller_setup(p_mtk_btif p_btif);
static int _btif_controller_free(p_mtk_btif p_btif);

static int _btif_pio_write(p_mtk_btif p_btif,
		    const unsigned char *p_buf, unsigned int buf_len);
static int _btif_dma_write(p_mtk_btif p_btif,
		    const unsigned char *p_buf, unsigned int buf_len);

static unsigned int btif_bbs_wr_direct(p_btif_buf_str p_bbs,
				unsigned char *p_buf, unsigned int buf_len);
static unsigned int btif_bbs_read(p_btif_buf_str p_bbs,
			   unsigned char *p_buf, unsigned int buf_len);
static unsigned int btif_bbs_write(p_btif_buf_str p_bbs,
			    unsigned char *p_buf, unsigned int buf_len);
static void btif_dump_bbs_str(unsigned char *p_str, p_btif_buf_str p_bbs);
static int _btif_dump_memory(char *str, unsigned char *p_buf, unsigned int buf_len);
static int _btif_rx_btm_deinit(p_mtk_btif p_btif);
static int _btif_rx_btm_sched(p_mtk_btif p_btif);
static int _btif_rx_btm_init(p_mtk_btif p_btif);
static void btif_rx_tasklet(unsigned long func_data);
static void btif_rx_worker(struct work_struct *p_work);
static int btif_rx_thread(void *p_data);
static int btif_rx_data_consummer(p_mtk_btif p_btif);

static int _btif_tx_ctx_init(p_mtk_btif p_btif);
static int _btif_tx_ctx_deinit(p_mtk_btif p_btif);
static void btif_tx_worker(struct work_struct *p_work);

static int _btif_state_deinit(p_mtk_btif p_btif);
static int _btif_state_release(p_mtk_btif p_btif);
static ENUM_BTIF_STATE _btif_state_get(p_mtk_btif p_btif);
static int _btif_state_set(p_mtk_btif p_btif, ENUM_BTIF_STATE state);
static int _btif_state_hold(p_mtk_btif p_btif);
static int _btif_state_init(p_mtk_btif p_btif);

static int _btif_dpidle_notify_ctrl(p_mtk_btif p_btif,
				    ENUM_BTIF_DPIDLE_CTRL en_flag);
static int _btif_enter_dpidle(p_mtk_btif p_btif);
static int _btif_exit_dpidle(p_mtk_btif p_btif);
static int _btif_exit_dpidle_from_sus(p_mtk_btif p_btif);
static int _btif_exit_dpidle_from_dpidle(p_mtk_btif p_btif);
static int _btif_enter_dpidle_from_on(p_mtk_btif p_btif);
static int _btif_enter_dpidle_from_sus(p_mtk_btif p_btif);

#if ENABLE_BTIF_TX_DMA
static int _btif_vfifo_deinit(p_mtk_btif_dma p_dma);
static int _btif_vfifo_init(p_mtk_btif_dma p_dma);
#endif

static bool _btif_is_tx_complete(p_mtk_btif p_btif);
static int _btif_init(p_mtk_btif p_btif);
static int _btif_lpbk_ctrl(p_mtk_btif p_btif, bool flag);
static int btif_rx_dma_mode_set(int en);
static int btif_tx_dma_mode_set(int en);

static int _btif_send_data(p_mtk_btif p_btif,
		    const unsigned char *p_buf, unsigned int buf_len);

/*-----------end of static function declearation----------------*/

static const char *g_state[B_S_MAX] = {
	"OFF",
	"SUSPEND",
	"DPIDLE",
	"ON",
};

/*-----------BTIF setting--------------*/
mtk_btif_setting g_btif_setting[BTIF_PORT_NR] = {
	{
	 .tx_mode = BTIF_TX_MODE,
	 .rx_mode = BTIF_RX_MODE,
	 .rx_type = BTIF_RX_BTM_CTX,
	 .tx_type = BTIF_TX_CTX,
	 },
};

mtk_btif g_btif[BTIF_PORT_NR] = {
	{
	 .open_counter = 0,
	 .state = B_S_OFF,
	 .setting = &g_btif_setting[0],
	 .p_tx_dma = NULL,
	 .p_rx_dma = NULL,
	 .rx_cb = NULL,
	 .p_btif_info = NULL,
	 },
};

mtk_btif_dma g_dma[BTIF_PORT_NR][BTIF_DIR_MAX] = {
	{
	 {
	  .p_btif = NULL,
	  .dir = BTIF_TX,
	  .p_dma_info = NULL,
	  .entry = ATOMIC_INIT(0),
	  },
	 {
	  .p_btif = NULL,
	  .dir = BTIF_RX,
	  .p_dma_info = NULL,
	  .entry = ATOMIC_INIT(0),
	  },
	 },
};

#define G_MAX_PKG_LEN (7 * 1024)
static int g_max_pkg_len = G_MAX_PKG_LEN; /*DMA vFIFO is set to 8 * 1024, we set this to 7/8 * vFIFO size*/
static int g_max_pding_data_size = BTIF_RX_BUFFER_SIZE * 3 / 4;


static int mtk_btif_dbg_lvl = BTIF_LOG_ERR;

#if BTIF_RXD_BE_BLOCKED_DETECT
static struct timeval btif_rxd_time_stamp[MAX_BTIF_RXD_TIME_REC];
#endif
/*-----------Platform bus related structures----------------*/
#define DRV_NAME "mtk_btif"

#ifdef CONFIG_OF
const struct of_device_id apbtif_of_ids[] = {
	{ .compatible = "mediatek,btif", },
	{}
};
#endif

const struct dev_pm_ops mtk_btif_drv_pm_ops = {
	.restore_noirq = mtk_btif_restore_noirq,
	.suspend = mtk_btif_drv_suspend,
	.resume = mtk_btif_drv_resume,
};

struct platform_driver mtk_btif_dev_drv = {
	.probe = mtk_btif_probe,
	.remove = mtk_btif_remove,
#ifdef CONFIG_PM
	.suspend = mtk_btif_suspend,
	.resume = mtk_btif_resume,
#endif
	.driver = {
			.name = DRV_NAME,
			.owner = THIS_MODULE,
#ifdef CONFIG_PM
			.pm = &mtk_btif_drv_pm_ops,
#endif
#ifdef CONFIG_OF
			.of_match_table = apbtif_of_ids,
#endif
		   }
};

#define BTIF_STATE_RELEASE(x) _btif_state_release(x)

/*-----------End of Platform bus related structures----------------*/

/*-----------platform bus related operation APIs----------------*/

static int mtk_btif_probe(struct platform_device *pdev)
{
/*Chaozhong: ToDo: to be implement*/
/*register IRQ for BTIF and Tx Rx DMA and disable them by default*/
	BTIF_INFO_FUNC("DO BTIF PROBE\n");
	platform_set_drvdata(pdev, &g_btif[0]);
	g_btif[0].private_data = (struct device *)&pdev->dev;

#if !defined(CONFIG_MTK_CLKMGR)
	hal_btif_clk_get_and_prepare(pdev);
#endif

	return 0;
}

static int mtk_btif_remove(struct platform_device *pdev)
{
/*Chaozhong: ToDo: to be implement*/
	BTIF_INFO_FUNC("DO BTIF REMOVE\n");
	platform_set_drvdata(pdev, NULL);
	g_btif[0].private_data = NULL;
	return 0;
}

int _btif_suspend(p_mtk_btif p_btif)
{
	int i_ret;

	if (_btif_state_hold(p_btif))
		return E_BTIF_INTR;
	if (p_btif != NULL) {
		if (!(p_btif->enable))
			i_ret = 0;
		else {
			if (_btif_state_get(p_btif) == B_S_ON) {
				BTIF_ERR_FUNC("BTIF in ON state,",
					"there are data need to be send or recev,suspend fail\n");
				i_ret = -1;
			} else {
				/*
				 * before disable BTIF controller and DMA controller
				 * we need to set BTIF to ON state
				 */
				i_ret = _btif_exit_dpidle(p_btif);
				if (i_ret == 0) {
					i_ret += _btif_controller_free(p_btif);
					i_ret = _btif_controller_tx_free(p_btif);
					i_ret += _btif_controller_rx_free(p_btif);
				}
				if (i_ret != 0) {
					BTIF_INFO_FUNC("failed\n");
					/*Chaozhong: what if failed*/
				} else {
					BTIF_INFO_FUNC("succeed\n");
					i_ret = _btif_state_set(p_btif, B_S_SUSPEND);
					if (i_ret && _btif_init(p_btif)) {
						/*Chaozhong:BTIF re-init failed? what to do*/
						i_ret = _btif_state_set(p_btif,	B_S_OFF);
					}
				}
			}
		}
	} else
		i_ret = -1;
	BTIF_STATE_RELEASE(p_btif);

	return i_ret;
}


static int mtk_btif_drv_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	pm_message_t state = PMSG_SUSPEND;

	return mtk_btif_suspend(pdev, state);
}

static int mtk_btif_drv_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	return mtk_btif_resume(pdev);
}

static int mtk_btif_suspend(struct platform_device *pdev, pm_message_t state)
{
	int i_ret = 0;
	p_mtk_btif p_btif = NULL;

/*Chaozhong: ToDo: to be implement*/
	BTIF_DBG_FUNC("++\n");
	p_btif = platform_get_drvdata(pdev);
	i_ret = _btif_suspend(p_btif);
	BTIF_DBG_FUNC("--, i_ret:%d\n", i_ret);
	return i_ret;
}

int _btif_restore_noirq(p_mtk_btif p_btif)
{
	int i_ret = 0;

/*BTIF IRQ restore no irq*/
	i_ret = hal_btif_pm_ops(p_btif->p_btif_info, BTIF_PM_RESTORE_NOIRQ);
	if (i_ret == 0) {
		BTIF_INFO_FUNC("BTIF HW IRQ restore succeed\n");
	} else {
		BTIF_INFO_FUNC("BTIF HW IRQ restore failed, i_ret:%d\n", i_ret);
		return i_ret;
	}
/*BTIF DMA restore no irq*/
	if (p_btif->tx_mode & BTIF_MODE_DMA) {
		i_ret = hal_dma_pm_ops(p_btif->p_tx_dma->p_dma_info,
				       BTIF_PM_RESTORE_NOIRQ);
		if (i_ret == 0) {
			BTIF_INFO_FUNC("BTIF Tx DMA IRQ restore succeed\n");
		} else {
			BTIF_INFO_FUNC
			    ("BTIF Tx DMA IRQ restore failed, i_ret:%d\n",
			     i_ret);
			return i_ret;
		}
	}
	if (p_btif->rx_mode & BTIF_MODE_DMA) {
		i_ret = hal_dma_pm_ops(p_btif->p_rx_dma->p_dma_info,
				       BTIF_PM_RESTORE_NOIRQ);
		if (i_ret == 0) {
			BTIF_INFO_FUNC("BTIF Rx DMA IRQ restore succeed\n");
		} else {
			BTIF_INFO_FUNC
			    ("BTIF Rx DMA IRQ restore failed, i_ret:%d\n",
			     i_ret);
			return i_ret;
		}
	}
	return i_ret;
}

static int mtk_btif_restore_noirq(struct device *dev)
{
	int i_ret = 0;
	struct platform_device *pdev = to_platform_device(dev);
	p_mtk_btif p_btif = platform_get_drvdata(pdev);

	BTIF_INFO_FUNC("++\n");
	if (_btif_state_hold(p_btif))
		return E_BTIF_INTR;
	if (p_btif->enable)
		BTIF_ERR_FUNC("!!!-----------------!BTIF is not closed before IPOH shutdown!!!---------------!\n");
	WARN_ON(p_btif->enable);

	i_ret = _btif_restore_noirq(p_btif);
	BTIF_STATE_RELEASE(p_btif);
	BTIF_INFO_FUNC("--\n");
	return 0;
}

int _btif_resume(p_mtk_btif p_btif)
{
	int i_ret = 0;
	ENUM_BTIF_STATE state = B_S_MAX;

	if (_btif_state_hold(p_btif))
		return E_BTIF_INTR;
	if (p_btif != NULL) {
		state = _btif_state_get(p_btif);
		if (!(p_btif->enable))
			i_ret = 0;
		else if (state == B_S_SUSPEND)
			i_ret = _btif_enter_dpidle(p_btif);
		else
			BTIF_INFO_FUNC
				("BTIF state: %s before resume, do nothing\n", g_state[state]);
	} else
		i_ret = -1;
	BTIF_STATE_RELEASE(p_btif);

	return i_ret;
}

static int mtk_btif_resume(struct platform_device *pdev)
{
	int i_ret = 0;
	p_mtk_btif p_btif = NULL;
/*Chaozhong: ToDo: to be implement*/
	BTIF_DBG_FUNC("++\n");
	p_btif = platform_get_drvdata(pdev);
	i_ret = _btif_resume(p_btif);
	BTIF_DBG_FUNC("--, i_ret:%d\n", i_ret);
	return 0;
}

/*-----------device node----------------*/
#if BTIF_CDEV_SUPPORT

dev_t btif_dev;
struct class *p_btif_class;
struct device *p_btif_dev;
const char *p_btif_dev_name = "btif";
static struct semaphore wr_mtx;
static struct semaphore rd_mtx;
unsigned char wr_buf[2048];
unsigned char rd_buf[2048];
static int rx_notify_flag;
static DECLARE_WAIT_QUEUE_HEAD(btif_wq);
static int btif_file_open(struct inode *pinode, struct file *pfile);
static int btif_file_release(struct inode *pinode, struct file *pfile);
static ssize_t btif_file_read(struct file *pfile,
			      char __user *buf, size_t count, loff_t *f_ops);

static ssize_t btif_file_write(struct file *filp,
			const char __user *buf, size_t count, loff_t *f_pos);
static long btif_unlocked_ioctl(struct file *filp, unsigned int cmd,
			 unsigned long arg);
#ifdef CONFIG_COMPAT
static long btif_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
#endif
static struct cdev btif_dev_c;
static wait_queue_head_t btif_read_inq;	/* read queues */

const struct file_operations mtk_btif_fops = {
	.owner = THIS_MODULE,
	.open = btif_file_open,
	.release = btif_file_release,
	.read = btif_file_read,
	.write = btif_file_write,
	.unlocked_ioctl = btif_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = btif_compat_ioctl,
#endif
	.poll = btif_poll,
};

static int btif_chrdev_init(void)
{
	int i_ret;

	int i_err;

	/* alloc device number dynamically */
	i_ret = alloc_chrdev_region(&btif_dev, 0, 1, p_btif_dev_name);
	if (i_ret) {
		BTIF_ERR_FUNC("devuce number allocation failed, i_ret:%d\n",
			      i_ret);
	} else {
		BTIF_INFO_FUNC("devuce number allocation succeed\n");
	}
	cdev_init(&btif_dev_c, &mtk_btif_fops);
	btif_dev_c.owner = THIS_MODULE;
	i_err = cdev_add(&btif_dev_c, btif_dev, 1);
	if (i_err) {
		BTIF_ERR_FUNC("error add btif dev to kernel, error code:%d\n",
			      i_err);
		unregister_chrdev_region(btif_dev, 1);
		btif_dev = 0;
		return -1;
	}
	BTIF_INFO_FUNC("add btif dev to kernel succeed\n");

	p_btif_class = class_create(THIS_MODULE, p_btif_dev_name);
	if (IS_ERR(p_btif_class)) {
		BTIF_ERR_FUNC("error happened when doing class_create\n");
		unregister_chrdev_region(btif_dev, 1);
		btif_dev = 0;
		return -2;
	}
	BTIF_INFO_FUNC("create class for btif succeed\n");

	p_btif_dev = device_create(p_btif_class,
				   NULL, btif_dev, 0, p_btif_dev_name);
	if (IS_ERR(p_btif_dev)) {
		BTIF_ERR_FUNC("error happened when doing device_create\n");
		class_destroy(p_btif_class);
		p_btif_class = NULL;
		unregister_chrdev_region(btif_dev, 1);
		btif_dev = 0;
		return -3;
	}
	BTIF_INFO_FUNC("create device for btif succeed\n");

	return 0;
}

void btif_rx_notify_cb(void)
{
	BTIF_DBG_FUNC("++\n");
	rx_notify_flag = 1;
	wake_up(&btif_wq);
	wake_up_interruptible(&btif_read_inq);
	BTIF_DBG_FUNC("--\n");
}

unsigned int btif_poll(struct file *filp, poll_table *wait)
{
	unsigned int mask = 0;
	unsigned int ava_len = 0;
/* btif_bbs_read(&(g_btif[0].btif_buf), rd_buf, sizeof(rd_buf)); */
	unsigned int wr_idx = g_btif[0].btif_buf.wr_idx;

/*    BTIF_Rx_IRQ_Disable(); */
	ava_len = BBS_COUNT_CUR(&(g_btif[0].btif_buf), wr_idx);
	BTIF_INFO_FUNC("++\n");
	if (ava_len == 0) {
		poll_wait(filp, &btif_read_inq, wait);
		wr_idx = g_btif[0].btif_buf.wr_idx;
		ava_len = BBS_COUNT_CUR(&(g_btif[0].btif_buf), wr_idx);
/* btif_bbs_read(&(g_btif[0].btif_buf), rd_buf, sizeof(rd_buf)); */
		if (ava_len)
			mask |= POLLIN | POLLRDNORM;	/* readable */
	} else {
		mask |= POLLIN | POLLRDNORM;	/* readable */
	}
/*make for writable*/
	mask |= POLLOUT | POLLWRNORM;	/* writable */
	BTIF_INFO_FUNC("--, mask:%d\n", mask);
	return mask;
}

static int _btif_file_open(void)
{
	int i_ret = -1;
	p_mtk_btif p_btif = &g_btif[0];

	BTIF_INFO_FUNC("++\n");

/*Chaozhong: ToDo: to be implement*/
	i_ret = btif_open(p_btif);
	if ((i_ret != 0) && (i_ret != E_BTIF_ALREADY_OPEN)) {
		BTIF_ERR_FUNC("btif_open failed, error code:%d\n", i_ret);
	} else {
		BTIF_INFO_FUNC("btif_open succeed\n");
		i_ret = 0;
	}
/*semaphore for read and write operation init*/
	sema_init(&wr_mtx, 1);
	sema_init(&rd_mtx, 1);

/*buffer for read and write init*/
	memset(wr_buf, 0, sizeof(wr_buf));
	memset(rd_buf, 0, sizeof(rd_buf));
	init_waitqueue_head(&(btif_read_inq));
	btif_rx_notify_reg(p_btif, btif_rx_notify_cb);
	BTIF_INFO_FUNC("--\n");
	return i_ret;
}

static int _btif_file_close(void)
{
	int i_ret = -1;

	BTIF_INFO_FUNC("++\n");
/*Chaozhong: ToDo: to be implement*/
	i_ret = btif_close(&g_btif[0]);
	if (i_ret != 0)
		BTIF_ERR_FUNC("btif_close failed, error code:%d\n", i_ret);
	else
		BTIF_INFO_FUNC("btif_close succeed\n");

	BTIF_INFO_FUNC("--\n");
	return i_ret;
}

static int btif_file_open(struct inode *pinode, struct file *pfile)
{
	int i_ret = -1;

	BTIF_INFO_FUNC("pid:%d\n", current->pid);
	i_ret = 0;
	return i_ret;
}

static int btif_file_release(struct inode *pinode, struct file *pfile)
{
	int i_ret = -1;

	BTIF_INFO_FUNC("pid:%d\n", current->pid);
	i_ret = 0;
	return i_ret;
}

static ssize_t btif_file_read(struct file *pfile,
			      char __user *buf, size_t count, loff_t *f_ops)
{
	int i_ret = 0;
	int rd_len = 0;

	BTIF_INFO_FUNC("++\n");
	down(&rd_mtx);
	rd_len = btif_bbs_read(&(g_btif[0].btif_buf), rd_buf, sizeof(rd_buf));
	while (rd_len == 0) {
		if (pfile->f_flags & O_NONBLOCK)
			break;

		wait_event(btif_wq, rx_notify_flag != 0);
		rx_notify_flag = 0;
		rd_len =
		    btif_bbs_read(&(g_btif[0].btif_buf), rd_buf,
				  sizeof(rd_buf));
	}

	if (rd_len == 0)
		i_ret = 0;
	else if ((rd_len > 0) && (copy_to_user(buf, rd_buf, rd_len) == 0))
		i_ret = rd_len;
	else
		i_ret = -EFAULT;

	up(&rd_mtx);
	BTIF_INFO_FUNC("--, i_ret:%d\n", i_ret);
	return i_ret;
}

ssize_t btif_file_write(struct file *filp,
			const char __user *buf, size_t count, loff_t *f_pos)
{
	int i_ret = 0;
	int copy_size = 0;

	copy_size = count > sizeof(wr_buf) ? sizeof(wr_buf) : count;

	BTIF_INFO_FUNC("++\n");
	down(&wr_mtx);
	if (copy_from_user(&wr_buf[0], &buf[0], copy_size))
		i_ret = -EFAULT;
	else
		i_ret = btif_send_data(&g_btif[0], wr_buf, copy_size);

	up(&wr_mtx);
	BTIF_INFO_FUNC("--, i_ret:%d\n", i_ret);

	return i_ret;
}
#ifdef CONFIG_COMPAT
long btif_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long ret;

	BTIF_INFO_FUNC("cmd[0x%x]\n", cmd);
	ret = btif_unlocked_ioctl(filp, cmd, arg);
	return ret;
}
#endif
long btif_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
#define BTIF_IOC_MAGIC        0xb0
#define BTIF_IOCTL_OPEN     _IOW(BTIF_IOC_MAGIC, 1, int)
#define BTIF_IOCTL_CLOSE    _IOW(BTIF_IOC_MAGIC, 2, int)
#define BTIF_IOCTL_LPBK_CTRL    _IOWR(BTIF_IOC_MAGIC, 3, int)
#define BTIF_IOCTL_LOG_FUNC_CTRL    _IOWR(BTIF_IOC_MAGIC, 4, int)
#define BTIF_IOCTL_RT_LOG_CTRL  _IOWR(BTIF_IOC_MAGIC, 5, int)
#define BTIF_IOCTL_LOG_DUMP _IOWR(BTIF_IOC_MAGIC, 6, int)
#define BTIF_IOCTL_REG_DUMP _IOWR(BTIF_IOC_MAGIC, 7, int)
#define BTIF_IOCTL_DMA_CTRL _IOWR(BTIF_IOC_MAGIC, 8, int)

	long ret = 0;
/* unsigned char p_buf[NAME_MAX + 1]; */
	p_mtk_btif p_btif = &g_btif[0];

	BTIF_INFO_FUNC("++\n");
	BTIF_DBG_FUNC("cmd (%u), arg (0x%lx)\n", cmd, arg);

	switch (cmd) {
	case BTIF_IOCTL_OPEN:
		ret = _btif_file_open();
		break;
	case BTIF_IOCTL_CLOSE:
		ret = _btif_file_close();
		break;
	case BTIF_IOCTL_LPBK_CTRL:
		ret = btif_lpbk_ctrl(p_btif, arg == 0 ? 0 : 1);
		break;
	case BTIF_IOCTL_LOG_FUNC_CTRL:
		if (arg == 0) {
			ret += btif_log_buf_disable(&p_btif->tx_log);
			ret += btif_log_buf_disable(&p_btif->rx_log);
		} else {
			ret += btif_log_buf_enable(&p_btif->tx_log);
			ret += btif_log_buf_enable(&p_btif->rx_log);
		}
		break;
	case BTIF_IOCTL_RT_LOG_CTRL:
		if (arg == 0) {
			ret += btif_log_output_disable(&p_btif->tx_log);
			ret += btif_log_output_disable(&p_btif->rx_log);
		} else {
			ret += btif_log_output_enable(&p_btif->tx_log);
			ret += btif_log_output_enable(&p_btif->rx_log);
		}
		break;
	case BTIF_IOCTL_LOG_DUMP:

		ret += btif_log_buf_dmp_out(&p_btif->tx_log);
		ret += btif_log_buf_dmp_out(&p_btif->rx_log);
		break;
	case BTIF_IOCTL_REG_DUMP:
		ret += btif_dump_reg(p_btif);
		break;
	case BTIF_IOCTL_DMA_CTRL:
		if (arg == 0) {
			ret += btif_tx_dma_mode_set(0);
			ret += btif_rx_dma_mode_set(0);
		} else {
			ret += btif_tx_dma_mode_set(1);
			ret += btif_rx_dma_mode_set(1);
		}
		break;
	default:
		BTIF_INFO_FUNC("unknown cmd(%d)\n", cmd);
		ret = -2;
		break;
	}
	BTIF_INFO_FUNC("--\n");
	return ret;
}

#endif

/*-----------device property----------------*/
//static ssize_t driver_flag_read(struct device_driver *drv, char *buf)
static ssize_t flag_show(struct device_driver *drv, char *buf)
{
	return sprintf(buf, "btif driver debug level:%d\n", mtk_btif_dbg_lvl);
}

//static ssize_t driver_flag_set(struct device_driver *drv,
static ssize_t flag_store(struct device_driver *drv,
			       const char *buffer, size_t count)
{
	char buf[256];
	char *p_buf;
	unsigned long len = count;
	long x = 0;
	long y = 0;
	long z = 0;
	int result = 0;
	char *p_token = NULL;
	char *p_delimiter = " \t";

	BTIF_INFO_FUNC("buffer = %s, count = %zd\n", buffer, count);
	if (len >= sizeof(buf)) {
		BTIF_ERR_FUNC("input handling fail!\n");
		len = sizeof(buf) - 1;
		return -1;
	}

	memcpy(buf, buffer, sizeof(buf));
	p_buf = buf;

	p_token = strsep(&p_buf, p_delimiter);
	if (p_token != NULL) {
		result = kstrtol(p_token, 16, &x);
		BTIF_INFO_FUNC("x = 0x%08x\n\r", x);
	} else
		x = 0;
/*	x = (NULL != p_token) ? kstrtol(p_token, 16, NULL) : 0;*/

	p_token = strsep(&p_buf, "\t\n ");
	if (p_token != NULL) {
		result = kstrtol(p_token, 16, &y);
		BTIF_INFO_FUNC("y = 0x%08x\n\r", y);
	} else
		y = 0;

	p_token = strsep(&p_buf, "\t\n ");
	if (p_token != NULL)
		result = kstrtol(p_token, 16, &z);
	else
		z = 0;

	BTIF_INFO_FUNC("x(0x%08x), y(0x%08x), z(0x%08x)\n\r", x, y, z);

	switch (x) {
	case 1:
		mtk_btif_exp_open_test();
		break;
	case 2:
		mtk_btif_exp_close_test();
		break;
	case 3:
		mtk_btif_exp_write_test();
		break;
	case 4:
		mtk_btif_exp_enter_dpidle_test();
		break;
	case 5:
		mtk_btif_exp_exit_dpidle_test();
		break;
	case 6:
		mtk_btif_exp_suspend_test();
		break;
	case 7:
		mtk_btif_exp_resume_test();
		break;
	case 8:
		if (y > BTIF_LOG_LOUD)
			mtk_btif_dbg_lvl = BTIF_LOG_LOUD;
		else if (y < BTIF_LOG_ERR)
			mtk_btif_dbg_lvl = BTIF_LOG_WARN;
		else
			mtk_btif_dbg_lvl = y;
		BTIF_ERR_FUNC("mtk_btif_dbg_lvl set to %d\n", mtk_btif_dbg_lvl);
		break;
	case 9:
		mtk_btif_exp_open_test();
		mtk_btif_exp_write_test();
		mtk_btif_exp_close_test();
		break;
	case 0xa:
		mtk_btif_exp_log_debug_test(y);
		break;
	case 0xb:
		btif_tx_dma_mode_set(1);
		btif_rx_dma_mode_set(1);
		break;
	case 0xc:
		btif_tx_dma_mode_set(0);
		btif_rx_dma_mode_set(0);
		break;
	case 0xd:
		mtk_btif_exp_restore_noirq_test();
		break;
	case 0xe:
		btif_wakeup_consys_no_id();
		break;
	case 0xf:
		mtk_btif_exp_clock_ctrl(y);
		break;
	case 0x10:
		y = y > G_MAX_PKG_LEN ? G_MAX_PKG_LEN : y;
		y = y < 1024 ? 1024 : y;
		BTIF_INFO_FUNC("g_max_pkg_len is set to %d\n", y);
		g_max_pkg_len = y;
		break;
	case 0x11:
		y = y > BTIF_RX_BUFFER_SIZE ? BTIF_RX_BUFFER_SIZE : y;
		y = y < 1024 ? 1024 : y;
		BTIF_INFO_FUNC("g_max_pding_data_size is set to %d\n", y);
		g_max_pding_data_size = y;
		break;
	default:
		mtk_btif_exp_open_test();
		mtk_btif_exp_write_stress_test(3030, 1);
		mtk_btif_exp_close_test();
		BTIF_WARN_FUNC("not supported.\n");
		break;
	}

	return count;
}

//FWU: driver_ATTR dropped in 4.14
//static DRIVER_ATTR(flag, S_IRUGO | S_IWUSR, driver_flag_read, driver_flag_set);
static DRIVER_ATTR_RW(flag);

/*-----------End of platform bus related operation APIs------------*/

/*-----------------------platform driver ----------------*/

int _btif_irq_reg(P_MTK_BTIF_IRQ_STR p_irq,
		  mtk_btif_irq_handler irq_handler, void *data)
{
	int i_ret = -1;
	unsigned int irq_id;
	unsigned int flag;

	if ((p_irq == NULL) || (irq_handler == NULL))
		return E_BTIF_INVAL_PARAM;

	if (!(p_irq->is_irq_sup)) {
		BTIF_WARN_FUNC("%s is not supported\n", p_irq->name);
		return 0;
	}

	irq_id = p_irq->irq_id;

#ifdef CONFIG_OF
	flag = p_irq->irq_flags;
#else
	switch (p_irq->sens_type) {
	case IRQ_SENS_EDGE:
		if (p_irq->edge_type == IRQ_EDGE_FALL)
			flag = IRQF_TRIGGER_FALLING;
		else if (p_irq->edge_type == IRQ_EDGE_RAISE)
			flag = IRQF_TRIGGER_RISING;
		else if (p_irq->edge_type == IRQ_EDGE_BOTH)
			flag = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING;
		else
			/*make this as default type */
			flag = IRQF_TRIGGER_FALLING;
		break;
	case IRQ_SENS_LVL:
		if (p_irq->lvl_type == IRQ_LVL_LOW)
			flag = IRQF_TRIGGER_LOW;
		else if (p_irq->lvl_type == IRQ_LVL_HIGH)
			flag = IRQF_TRIGGER_HIGH;
		else
			/*make this as default type */
			flag = IRQF_TRIGGER_LOW;
		break;
	default:
		/*make this as default type */
		flag = IRQF_TRIGGER_LOW;
		break;
	}
#endif

	p_irq->p_irq_handler = irq_handler;
	i_ret = request_irq(irq_id,
			    (irq_handler_t) irq_handler,
			    flag, p_irq->name, data);
	if (i_ret)
		return i_ret;

	p_irq->reg_flag = true;
	return 0;
}

int _btif_irq_free(P_MTK_BTIF_IRQ_STR p_irq, void *data)
{
	int i_ret = 0;
	unsigned int eint_num = p_irq->irq_id;

	if ((p_irq->is_irq_sup) && (p_irq->reg_flag)) {
		_btif_irq_ctrl(p_irq, false);
		free_irq(eint_num, data);
		p_irq->reg_flag = false;
	}
/*do nothing for this operation*/
	return i_ret;
}

int _btif_irq_ctrl(P_MTK_BTIF_IRQ_STR p_irq, bool en)
{
	unsigned int eint_num = p_irq->irq_id;

	if (en)
		enable_irq(eint_num);
	else
		disable_irq_nosync(eint_num);

	return 0;
}

int _btif_irq_ctrl_sync(P_MTK_BTIF_IRQ_STR p_irq, bool en)
{
	unsigned int eint_num = p_irq->irq_id;

	if (en)
		enable_irq(eint_num);
	else
		disable_irq(eint_num);

	return 0;
}


irqreturn_t btif_irq_handler(int irq, void *data)
{
/*search BTIF? just use index 0*/
/*Chaozhong: do we need lock here?*/

	p_mtk_btif p_btif = (p_mtk_btif) data;	/*&(g_btif[index]); */

	BTIF_DBG_FUNC("++, p_btif(0x%p)\n", data);

	_btif_irq_ctrl(p_btif->p_btif_info->p_irq, false);

#if MTK_BTIF_ENABLE_CLK_REF_COUNTER
	hal_btif_clk_ctrl(p_btif->p_btif_info, CLK_OUT_ENABLE);
#endif

	hal_btif_irq_handler(p_btif->p_btif_info, NULL, 0);

#if MTK_BTIF_ENABLE_CLK_REF_COUNTER
	hal_btif_clk_ctrl(p_btif->p_btif_info, CLK_OUT_DISABLE);
#endif

	_btif_irq_ctrl(p_btif->p_btif_info->p_irq, true);
	_btif_rx_btm_sched(p_btif);

	BTIF_DBG_FUNC("--\n");
	return IRQ_HANDLED;
}

irqreturn_t btif_tx_dma_irq_handler(int irq, void *data)
{
/*search BTIF? just use index 0*/

	p_mtk_btif p_btif = (p_mtk_btif) data;	/*&(g_btif[index]); */
	p_mtk_btif_dma p_tx_dma = p_btif->p_tx_dma;
	P_MTK_DMA_INFO_STR p_dma_info = p_tx_dma->p_dma_info;

	BTIF_DBG_FUNC("++, p_btif(0x%p)\n", data);
	_btif_irq_ctrl(p_dma_info->p_irq, false);

#if MTK_BTIF_ENABLE_CLK_REF_COUNTER
	hal_btif_dma_clk_ctrl(p_dma_info, CLK_OUT_ENABLE);
#endif

	hal_tx_dma_irq_handler(p_dma_info);

#if MTK_BTIF_ENABLE_CLK_REF_COUNTER
	hal_btif_dma_clk_ctrl(p_dma_info, CLK_OUT_DISABLE);
#endif
	_btif_irq_ctrl(p_dma_info->p_irq, true);
	BTIF_DBG_FUNC("--\n");
	return IRQ_HANDLED;
}

irqreturn_t btif_rx_dma_irq_handler(int irq, void *data)
{
/*search BTIF? just use index 0*/

	p_mtk_btif p_btif = (p_mtk_btif) data;	/*&(g_btif[index]); */
	p_mtk_btif_dma p_rx_dma = p_btif->p_rx_dma;
	P_MTK_DMA_INFO_STR p_rx_dma_info = p_rx_dma->p_dma_info;

	BTIF_DBG_FUNC("++, p_btif(0x%p)\n", data);

	_btif_irq_ctrl(p_rx_dma_info->p_irq, false);

#if MTK_BTIF_ENABLE_CLK_REF_COUNTER
	hal_btif_clk_ctrl(p_btif->p_btif_info, CLK_OUT_ENABLE);
	hal_btif_dma_clk_ctrl(p_rx_dma_info, CLK_OUT_ENABLE);
#endif

	hal_rx_dma_irq_handler(p_rx_dma_info, NULL, 0);

#if MTK_BTIF_ENABLE_CLK_REF_COUNTER
	hal_btif_dma_clk_ctrl(p_rx_dma_info, CLK_OUT_DISABLE);
	hal_btif_clk_ctrl(p_btif->p_btif_info, CLK_OUT_DISABLE);
#endif

	_btif_irq_ctrl(p_rx_dma_info->p_irq, true);

	_btif_rx_btm_sched(p_btif);

	BTIF_DBG_FUNC("--\n");

	return IRQ_HANDLED;
}

unsigned int btif_dma_rx_data_receiver(P_MTK_DMA_INFO_STR p_dma_info,
				       unsigned char *p_buf,
				       unsigned int buf_len)
{
	unsigned int index = 0;
	p_mtk_btif p_btif = &(g_btif[index]);

#if 0
	_btif_dump_memory("<DMA Rx>", p_buf, buf_len);
#endif

	btif_bbs_write(&(p_btif->btif_buf), p_buf, buf_len);
/*save DMA Rx packet here*/
	if (buf_len > 0)
		btif_log_buf_dmp_in(&p_btif->rx_log, p_buf, buf_len);

	return 0;
}

unsigned int btif_pio_rx_data_receiver(P_MTK_BTIF_INFO_STR p_btif_info,
				       unsigned char *p_buf,
				       unsigned int buf_len)
{
	unsigned int index = 0;
	p_mtk_btif p_btif = &(g_btif[index]);

#if 0
	_btif_dump_memory("<PIO Rx>", p_buf, buf_len);
#endif
	btif_bbs_write(&(p_btif->btif_buf), p_buf, buf_len);

/*save PIO Rx packet here*/
	if (buf_len > 0)
		btif_log_buf_dmp_in(&p_btif->rx_log, p_buf, buf_len);

	return 0;
}

bool btif_parser_wmt_evt(p_mtk_btif p_btif,
				const char *sub_str,
				unsigned int str_len)
{
	unsigned int data_cnt = 0;
	unsigned int copy_cnt = 0;
	char *local_buf = NULL;
	bool b_ret = false;
	p_btif_buf_str p_bbs = &(p_btif->btif_buf);
	unsigned int wr_idx = p_bbs->wr_idx;
	unsigned int rd_idx = p_bbs->rd_idx;

	data_cnt = copy_cnt =  BBS_COUNT(p_bbs);

	if (data_cnt < str_len) {
		BTIF_WARN_FUNC("there is not enough data for parser,need(%d),have(%d)\n", str_len, data_cnt);
		return false;
	}
	BTIF_INFO_FUNC("data count in bbs buffer:%d,wr_idx(%d),rd_idx(%d)\n", data_cnt, wr_idx, rd_idx);
	local_buf = vmalloc((data_cnt + 3) & ~0x3UL);
	if (!local_buf) {
		BTIF_WARN_FUNC("vmalloc memory fail\n");
		return false;
	}

	if (wr_idx >= rd_idx) {
		memcpy(local_buf, BBS_PTR(p_bbs, rd_idx), copy_cnt);
	} else {
		unsigned int tail_len = BBS_SIZE(p_bbs) - rd_idx;

		BTIF_INFO_FUNC("tail_Len(%d)\n", tail_len);
		memcpy(local_buf, BBS_PTR(p_bbs, rd_idx), tail_len);
		memcpy(local_buf + tail_len, BBS_PTR(p_bbs, 0), copy_cnt - tail_len);
	}

	do {
		int i = 0;
		int j = 0;
		int k = 0;
		int d = 0;

		BTIF_INFO_FUNC("sub_str_len:%d\n", str_len);
		for (i = 0; i < copy_cnt; i++) {
			BTIF_DBG_FUNC("i:%d\n", i);
			k = i;
			while (1) {
				if ((j >= str_len) || (k >= copy_cnt) || (sub_str[j++] != local_buf[k++]))
					break;
			}

			if (j == str_len) {
				for (d = i; d < (str_len + i); d++)
					BTIF_INFO_FUNC("0x%2x", local_buf[d]);
				BTIF_INFO_FUNC("find sub str index:%d\n", i);
				b_ret = true;
				break;
			}
			if (j < str_len)
				j = 0;
		}

	} while (0);

	vfree(local_buf);
	return b_ret;
}
int _btif_controller_tx_setup(p_mtk_btif p_btif)
{
	int i_ret = -1;

	if (p_btif->tx_mode == BTIF_MODE_DMA) {
		i_ret = _btif_tx_dma_setup(p_btif);
		if (i_ret) {
			BTIF_ERR_FUNC("_btif_tx_dma_setup failed,i_ret(%d),",
				"set tx to PIO mode\n", i_ret);
			i_ret = _btif_tx_pio_setup(p_btif);
		}
	} else
/*enable Tx PIO mode*/
		i_ret = _btif_tx_pio_setup(p_btif);

	return i_ret;
}

int _btif_controller_tx_free(p_mtk_btif p_btif)
{
	int i_ret = -1;

	if (p_btif->tx_mode == BTIF_MODE_DMA) {
		i_ret = _btif_tx_dma_free(p_btif);
		if (i_ret) {
			BTIF_ERR_FUNC("_btif_tx_dma_free failed, i_ret(%d)\n",
				      i_ret);
		}
	} else {
/*do nothing for Tx PIO mode*/
	}
	return i_ret;
}

int _btif_controller_rx_setup(p_mtk_btif p_btif)
{
	int i_ret = -1;

	if (p_btif->rx_mode == BTIF_MODE_DMA) {
		i_ret = _btif_rx_dma_setup(p_btif);
		if (i_ret) {
			BTIF_ERR_FUNC("_btif_tx_dma_setup failed, i_ret(%d),",
				"set tx to PIO mode\n", i_ret);
			i_ret = _btif_rx_pio_setup(p_btif);
		}
	} else {
/*enable Tx PIO mode*/
		i_ret = _btif_rx_pio_setup(p_btif);
	}
	return i_ret;
}

int _btif_controller_rx_free(p_mtk_btif p_btif)
{
	int i_ret = -1;

	if (p_btif->rx_mode == BTIF_MODE_DMA) {
		i_ret = _btif_rx_dma_free(p_btif);
		if (i_ret) {
			BTIF_ERR_FUNC("_btif_rx_dma_free failed, i_ret(%d)\n",
				      i_ret);
		}
	} else {
/*do nothing for Rx PIO mode*/
	}
	return i_ret;
}

int _btif_tx_pio_setup(p_mtk_btif p_btif)
{
	int i_ret = -1;
	P_MTK_BTIF_INFO_STR p_btif_info = p_btif->p_btif_info;

/*set Tx to PIO mode*/
	p_btif->tx_mode = BTIF_MODE_PIO;
/*enable Tx PIO mode*/
	i_ret = hal_btif_tx_mode_ctrl(p_btif_info, BTIF_MODE_PIO);
	return i_ret;
}

int _btif_rx_pio_setup(p_mtk_btif p_btif)
{
	int i_ret = -1;
	P_MTK_BTIF_INFO_STR p_btif_info = p_btif->p_btif_info;
	P_MTK_BTIF_IRQ_STR p_btif_irq = p_btif_info->p_irq;

	p_btif->rx_mode = BTIF_MODE_PIO;
/*Enable Rx IRQ*/
	_btif_irq_ctrl(p_btif_irq, true);
/*enable Rx PIO mode*/
	i_ret = hal_btif_rx_mode_ctrl(p_btif_info, BTIF_MODE_PIO);
	return i_ret;
}

int _btif_rx_dma_setup(p_mtk_btif p_btif)
{
	int i_ret = -1;
	P_MTK_BTIF_INFO_STR p_btif_info = NULL;
	P_MTK_BTIF_IRQ_STR p_btif_irq = NULL;
	P_MTK_DMA_INFO_STR p_dma_info = p_btif->p_rx_dma->p_dma_info;

	p_btif_info = p_btif->p_btif_info;
	p_btif_irq = p_dma_info->p_irq;

/*vFIFO reset*/
	hal_btif_vfifo_reset(p_dma_info);

	i_ret = hal_btif_dma_clk_ctrl(p_dma_info, CLK_OUT_ENABLE);
	if (i_ret) {
		BTIF_ERR_FUNC("hal_btif_dma_clk_ctrl failed, i_ret(%d),",
			"set rx to pio mode\n", i_ret);
/*DMA control failed set Rx to PIO mode*/
		return _btif_rx_pio_setup(p_btif);
	}
/*hardware init*/
	hal_btif_dma_hw_init(p_dma_info);

	hal_btif_dma_rx_cb_reg(p_dma_info,
			       (dma_rx_buf_write) btif_dma_rx_data_receiver);

/*DMA controller enable*/
	i_ret = hal_btif_dma_ctrl(p_dma_info, DMA_CTRL_ENABLE);
	if (i_ret) {
		BTIF_ERR_FUNC("hal_btif_dma_ctrl failed, i_ret(%d),",
			"set rx to pio mode\n", i_ret);
		hal_btif_dma_clk_ctrl(p_dma_info, CLK_OUT_DISABLE);
/*DMA control failed set Rx to PIO mode*/
		i_ret = _btif_rx_pio_setup(p_btif);
	} else {
/*enable Rx DMA mode*/
		hal_btif_rx_mode_ctrl(p_btif_info, BTIF_MODE_DMA);

/*DMA Rx IRQ register*/
		_btif_irq_reg(p_btif_irq, btif_rx_dma_irq_handler, p_btif);
#if 0
/*Enable DMA Rx IRQ*/
		_btif_irq_ctrl(p_btif_irq, true);
#endif
		BTIF_DBG_FUNC("succeed\n");
	}
	return i_ret;
}

int _btif_rx_dma_free(p_mtk_btif p_btif)
{
	P_MTK_DMA_INFO_STR p_dma_info = p_btif->p_rx_dma->p_dma_info;
	P_MTK_BTIF_IRQ_STR p_irq = p_btif->p_rx_dma->p_dma_info->p_irq;

	hal_btif_dma_rx_cb_reg(p_dma_info, (dma_rx_buf_write) NULL);
	_btif_irq_free(p_irq, p_btif);
/*disable BTIF Rx DMA channel*/
	hal_btif_dma_ctrl(p_dma_info, DMA_CTRL_DISABLE);
/*disable clock output*/
	return hal_btif_dma_clk_ctrl(p_dma_info, CLK_OUT_DISABLE);
}

int _btif_tx_dma_setup(p_mtk_btif p_btif)
{
	int i_ret = -1;
	P_MTK_BTIF_INFO_STR p_btif_info = p_btif->p_btif_info;
	P_MTK_DMA_INFO_STR p_dma_info = p_btif->p_tx_dma->p_dma_info;
	P_MTK_BTIF_IRQ_STR p_btif_irq = p_dma_info->p_irq;

/*vFIFO reset*/
	hal_btif_vfifo_reset(p_dma_info);

	i_ret = hal_btif_dma_clk_ctrl(p_dma_info, CLK_OUT_ENABLE);
	if (i_ret) {
		BTIF_ERR_FUNC("hal_btif_dma_clk_ctrl failed, i_ret(%d)\n",
			      i_ret);
		return i_ret;
	}
/*DMA controller setup*/
	hal_btif_dma_hw_init(p_dma_info);

/*DMA HW Enable*/
	i_ret = hal_btif_dma_ctrl(p_dma_info, DMA_CTRL_ENABLE);
	if (i_ret) {
		BTIF_ERR_FUNC("hal_btif_dma_ctrl failed, i_ret(%d),",
			"set tx to pio mode\n", i_ret);

#if !(MTK_BTIF_ENABLE_CLK_REF_COUNTER)
		hal_btif_dma_clk_ctrl(p_dma_info, CLK_OUT_DISABLE);
#endif

		_btif_tx_pio_setup(p_btif);
	} else {
		hal_btif_tx_mode_ctrl(p_btif_info, BTIF_MODE_DMA);
/*DMA Tx IRQ register*/
		_btif_irq_reg(p_btif_irq, btif_tx_dma_irq_handler, p_btif);
#if 0
/*disable DMA Tx IRQ*/
		_btif_irq_ctrl(p_btif_irq, false);
#endif

		BTIF_DBG_FUNC("succeed\n");
	}
	return i_ret;
}

int _btif_tx_dma_free(p_mtk_btif p_btif)
{
	P_MTK_DMA_INFO_STR p_dma_info = p_btif->p_tx_dma->p_dma_info;
	P_MTK_BTIF_IRQ_STR p_irq = p_btif->p_tx_dma->p_dma_info->p_irq;

	_btif_irq_free(p_irq, p_btif);
/*disable BTIF Tx DMA channel*/
	hal_btif_dma_ctrl(p_dma_info, DMA_CTRL_DISABLE);
/*disable clock output*/
	return hal_btif_dma_clk_ctrl(p_dma_info, CLK_OUT_DISABLE);
}

int btif_lpbk_ctrl(p_mtk_btif p_btif, bool flag)
{
	int i_ret = -1;

/*hold state mechine lock*/
	if (_btif_state_hold(p_btif))
		return E_BTIF_INTR;
#if 0
	state = _btif_state_get(p_btif);
	if (p_btif->enable && B_S_ON == state)
		i_ret = _btif_lpbk_ctrl(p_btif, flag);
	else
		i_ret = E_BTIF_INVAL_STATE;
#endif
	i_ret = _btif_exit_dpidle(p_btif);
	if (i_ret == 0)
		i_ret = _btif_lpbk_ctrl(p_btif, flag);
	else
		i_ret = E_BTIF_INVAL_STATE;

	BTIF_STATE_RELEASE(p_btif);
	return i_ret;
}

int _btif_lpbk_ctrl(p_mtk_btif p_btif, bool flag)
{
	int i_ret = -1;

	if (flag) {
		i_ret = hal_btif_loopback_ctrl(p_btif->p_btif_info, true);
		BTIF_DBG_FUNC("loopback function enabled\n");
	} else {
		i_ret = hal_btif_loopback_ctrl(p_btif->p_btif_info, false);
		BTIF_DBG_FUNC("loopback function disabled\n");
	}
	if (i_ret == 0)
		p_btif->lpbk_flag = flag;

	return i_ret;
}

int btif_clock_ctrl(p_mtk_btif p_btif, int en)
{
	int i_ret = 0;
	P_MTK_BTIF_INFO_STR p_btif_info = p_btif->p_btif_info;
	ENUM_CLOCK_CTRL ctrl_flag = en == 0 ? CLK_OUT_DISABLE : CLK_OUT_ENABLE;

	i_ret = hal_btif_clk_ctrl(p_btif_info, ctrl_flag);

	if (p_btif->rx_mode == BTIF_MODE_DMA)
		i_ret += hal_btif_dma_clk_ctrl(p_btif->p_rx_dma->p_dma_info, ctrl_flag);

	if (p_btif->tx_mode == BTIF_MODE_DMA)
		i_ret += hal_btif_dma_clk_ctrl(p_btif->p_tx_dma->p_dma_info, ctrl_flag);

	return i_ret;
}

int _btif_controller_setup(p_mtk_btif p_btif)
{
	int i_ret = -1;
	P_MTK_BTIF_INFO_STR p_btif_info = p_btif->p_btif_info;
	P_MTK_BTIF_IRQ_STR p_btif_irq = p_btif_info->p_irq;

/*BTIF rx buffer init*/
/* memset(p_btif->rx_buf, 0, BTIF_RX_BUFFER_SIZE); */
	BBS_INIT(&(p_btif->btif_buf));
/************************************************/
	hal_btif_rx_cb_reg(p_btif_info,
			   (btif_rx_buf_write) btif_pio_rx_data_receiver);

	i_ret = hal_btif_clk_ctrl(p_btif_info, CLK_OUT_ENABLE);
	if (i_ret) {
		BTIF_ERR_FUNC("hal_btif_clk_ctrl failed, i_ret(%d)\n", i_ret);
		return i_ret;
	}
/*BTIF controller init*/
	i_ret = hal_btif_hw_init(p_btif_info);
	if (i_ret) {
		hal_btif_clk_ctrl(p_btif_info, CLK_OUT_DISABLE);
		BTIF_ERR_FUNC("hal_btif_hw_init failed, i_ret(%d)\n", i_ret);
		return i_ret;
	}
	_btif_lpbk_ctrl(p_btif, p_btif->lpbk_flag);
/*BTIF IRQ register*/
	i_ret = _btif_irq_reg(p_btif_irq, btif_irq_handler, p_btif);
	if (i_ret) {
		hal_btif_clk_ctrl(p_btif_info, CLK_OUT_DISABLE);

		BTIF_ERR_FUNC("_btif_irq_reg failed, i_ret(%d)\n", i_ret);
		return i_ret;
	}

/*disable IRQ*/
	_btif_irq_ctrl(p_btif_irq, false);
	i_ret = 0;
	BTIF_DBG_FUNC("succeed\n");
	return i_ret;
}

int _btif_controller_free(p_mtk_btif p_btif)
{
/*No need to set BTIF to PIO mode, only enable BTIF CG*/
	hal_btif_rx_cb_reg(p_btif->p_btif_info, (btif_rx_buf_write) NULL);
	_btif_irq_free(p_btif->p_btif_info->p_irq, p_btif);
	return hal_btif_clk_ctrl(p_btif->p_btif_info, CLK_OUT_DISABLE);
}

int _btif_init(p_mtk_btif p_btif)
{
	int i_ret = 0;

	i_ret = _btif_controller_setup(p_btif);
	if (i_ret) {
		BTIF_ERR_FUNC("_btif_controller_init failed, i_ret(%d)\n",
			      i_ret);
		_btif_dpidle_notify_ctrl(p_btif, BTIF_DPIDLE_ENABLE);
		BTIF_STATE_RELEASE(p_btif);
		return i_ret;
	}

	i_ret = _btif_controller_tx_setup(p_btif);
	if (i_ret) {
		BTIF_ERR_FUNC("_btif_controller_tx_setup failed, i_ret(%d)\n",
			      i_ret);
		_btif_controller_free(p_btif);
		_btif_dpidle_notify_ctrl(p_btif, BTIF_DPIDLE_ENABLE);
		BTIF_STATE_RELEASE(p_btif);
		return i_ret;
	}

	i_ret = _btif_controller_rx_setup(p_btif);
	if (i_ret) {
		BTIF_ERR_FUNC("_btif_controller_tx_setup failed, i_ret(%d)\n",
			      i_ret);
		_btif_controller_tx_free(p_btif);
		_btif_controller_free(p_btif);
		_btif_dpidle_notify_ctrl(p_btif, BTIF_DPIDLE_ENABLE);
		BTIF_STATE_RELEASE(p_btif);
		return i_ret;
	}
	return i_ret;
}

int btif_open(p_mtk_btif p_btif)
{
	int i_ret = -1;

	if (p_btif->enable)
		return E_BTIF_ALREADY_OPEN;

/*hold state mechine lock*/
	if (_btif_state_hold(p_btif))
		return E_BTIF_INTR;
/*disable deepidle*/
	_btif_dpidle_notify_ctrl(p_btif, BTIF_DPIDLE_DISABLE);

	i_ret = _btif_init(p_btif);
	if (i_ret == 0) {
		/*set BTIF's enable flag*/
		p_btif->enable = true;
		_btif_state_set(p_btif, B_S_ON);
	} else {
		_btif_dpidle_notify_ctrl(p_btif, BTIF_DPIDLE_ENABLE);
	}
	btif_log_buf_reset(&p_btif->tx_log);
	btif_log_buf_reset(&p_btif->rx_log);

	BTIF_STATE_RELEASE(p_btif);

	BTIF_DBG_FUNC("BTIF's Tx Mode:%d, Rx Mode(%d)\n",
		       p_btif->tx_mode, p_btif->rx_mode);
	return i_ret;
}

int btif_close(p_mtk_btif p_btif)
{
	int i_ret = 0;

	if (!(p_btif->enable))
		return E_BTIF_NOT_OPEN;

/*hold state mechine lock*/
	if (_btif_state_hold(p_btif))
		return E_BTIF_INTR;
/*always set state back to B_S_ON before do close operation*/
	_btif_exit_dpidle(p_btif);
/*set BTIF's state to disable state*/
	p_btif->enable = false;

	_btif_controller_free(p_btif);
	_btif_controller_tx_free(p_btif);
	_btif_controller_rx_free(p_btif);

/*reset BTIF's rx_cb function*/
	p_btif->rx_cb = NULL;
	p_btif->rx_notify = NULL;
	p_btif->lpbk_flag = false;

/*set state mechine to B_S_OFF*/
	_btif_state_set(p_btif, B_S_OFF);

	btif_log_buf_disable(&p_btif->tx_log);
	btif_log_buf_disable(&p_btif->rx_log);

	BTIF_STATE_RELEASE(p_btif);

	return i_ret;
}

int _btif_exit_dpidle(p_mtk_btif p_btif)
{
	int i_ret = -1;
	ENUM_BTIF_STATE state = B_S_MAX;

	state = _btif_state_get(p_btif);
	switch (state) {
	case B_S_DPIDLE:
		i_ret = _btif_exit_dpidle_from_dpidle(p_btif);
		break;
	case B_S_SUSPEND:
/*in suspend state, need to do reinit of btif*/
		i_ret = _btif_exit_dpidle_from_sus(p_btif);
		break;
	case B_S_OFF:
		i_ret = _btif_init(p_btif);
		break;
	case B_S_ON:
		i_ret = 0;	/* for btif_close case */
		break;
	default:
		i_ret = E_BTIF_INVAL_PARAM;
		BTIF_INFO_FUNC("invalid state change:%d->\n", state, B_S_ON);
		break;
	}

	if (i_ret == 0)
		i_ret = _btif_state_set(p_btif, B_S_ON);
	return i_ret;
}

int btif_exit_dpidle(p_mtk_btif p_btif)
{
	int i_ret = 0;

/*hold state mechine lock*/
	if (_btif_state_hold(p_btif))
		return E_BTIF_INTR;
	i_ret = _btif_exit_dpidle(p_btif);
	BTIF_STATE_RELEASE(p_btif);
	return i_ret;
}

int _btif_enter_dpidle(p_mtk_btif p_btif)
{
	int i_ret = 0;
	ENUM_BTIF_STATE state = B_S_MAX;

	state = _btif_state_get(p_btif);
	if (state == B_S_ON) {
		i_ret = _btif_enter_dpidle_from_on(p_btif);
	} else if (state == B_S_SUSPEND) {
		/*do reinit and enter deepidle*/
		i_ret = _btif_enter_dpidle_from_sus(p_btif);
	} else if (state == B_S_DPIDLE) {
		/*do nothing*/
		i_ret = 0;
	} else {
		BTIF_WARN_FUNC("operation is not allowed, current state:%d\n",
			       state);
		i_ret = E_BTIF_INVAL_STATE;
	}
/*anyway, set to B_S_DPIDLE state*/
	if (i_ret == 0)
		i_ret = _btif_state_set(p_btif, B_S_DPIDLE);
	return i_ret;
}

int btif_enter_dpidle(p_mtk_btif p_btif)
{
	int i_ret = 0;

/*hold state mechine lock*/
	if (_btif_state_hold(p_btif))
		return E_BTIF_INTR;
	i_ret = _btif_enter_dpidle(p_btif);
	BTIF_STATE_RELEASE(p_btif);
	return i_ret;
}

int _btif_exit_dpidle_from_dpidle(p_mtk_btif p_btif)
{
	int i_ret = 0;

/*in dpidle state, only need to open related clock*/
	if (p_btif->tx_mode == BTIF_MODE_DMA) {
		/*enable BTIF Tx DMA's clock*/
		i_ret += hal_btif_dma_clk_ctrl(p_btif->p_tx_dma->p_dma_info,
					       CLK_OUT_ENABLE);
	}
	if (p_btif->rx_mode == BTIF_MODE_DMA) {
		/*enable BTIF Rx DMA's clock*/
		i_ret += hal_btif_dma_clk_ctrl(p_btif->p_rx_dma->p_dma_info,
					       CLK_OUT_ENABLE);
	}
/*enable BTIF's clock*/
	i_ret += hal_btif_clk_ctrl(p_btif->p_btif_info, CLK_OUT_ENABLE);

	if (i_ret != 0)
		BTIF_WARN_FUNC("failed, i_ret:%d\n", i_ret);
	return i_ret;
}

int _btif_exit_dpidle_from_sus(p_mtk_btif p_btif)
{
/*in suspend state, need to do driver re-init*/

	int i_ret = _btif_init(p_btif);

	return i_ret;
}

int _btif_enter_dpidle_from_sus(p_mtk_btif p_btif)
{
/*do driiver reinit*/
	int i_ret = _btif_init(p_btif);

	if (i_ret == 0)
		i_ret = _btif_enter_dpidle_from_on(p_btif);
	return i_ret;
}

int _btif_enter_dpidle_from_on(p_mtk_btif p_btif)
{
#define MAX_WAIT_TIME_MS 5000
/*
 * this max wait time cannot exceed 12s,
 * because dpm will monitor each device's
 * resume/suspend process by start up a watch dog timer of 12s
 * incase of one driver's suspend/resume process block other device's suspend/resume
 */
	int i_ret = 0;
	unsigned int retry = 0;
	unsigned int wait_period = 1;
	unsigned int max_retry = MAX_WAIT_TIME_MS / wait_period;
	struct timeval timer_start;
	struct timeval timer_now;

	do_gettimeofday(&timer_start);

	while ((!_btif_is_tx_complete(p_btif)) && (retry < max_retry)) {
		do_gettimeofday(&timer_now);
		if ((MAX_WAIT_TIME_MS/1000) <= (timer_now.tv_sec - timer_start.tv_sec)) {
			BTIF_WARN_FUNC("max retry timer expired, timer_start.tv_sec:%d, timer_now.tv_sec:%d,",
				"retry:%d\n", timer_start.tv_sec, timer_now.tv_sec, retry);
			break;
		}
		msleep(wait_period);
		retry++;
	}

	if (retry < max_retry) {
		if (p_btif->tx_mode == BTIF_MODE_DMA) {
			/*disable BTIF Tx DMA's clock*/
			i_ret +=
			    hal_btif_dma_clk_ctrl(p_btif->p_tx_dma->p_dma_info,
						  CLK_OUT_DISABLE);
		}
		if (p_btif->rx_mode == BTIF_MODE_DMA) {
			/*disable BTIF Rx DMA's clock*/
			i_ret +=
			    hal_btif_dma_clk_ctrl(p_btif->p_rx_dma->p_dma_info,
						  CLK_OUT_DISABLE);
		}
/*disable BTIF's clock*/
		i_ret +=
		    hal_btif_clk_ctrl(p_btif->p_btif_info, CLK_OUT_DISABLE);

		if (i_ret)
			BTIF_WARN_FUNC("failed, i_ret:%d\n", i_ret);
	} else
		i_ret = -1;

	return i_ret;
}

int _btif_dpidle_notify_ctrl(p_mtk_btif p_btif, ENUM_BTIF_DPIDLE_CTRL en_flag)
{
/*call WCP's API to control deepidle's enable/disable*/
	if (en_flag == BTIF_DPIDLE_DISABLE)
		hal_btif_pm_ops(p_btif->p_btif_info, BTIF_PM_DPIDLE_DIS);
	else
		hal_btif_pm_ops(p_btif->p_btif_info, BTIF_PM_DPIDLE_EN);

	return 0;
}

int btif_rx_cb_reg(p_mtk_btif p_btif, MTK_WCN_BTIF_RX_CB rx_cb)
{
	if (p_btif->rx_cb) {
		BTIF_WARN_FUNC
		    ("rx cb already exist, rewrite from (0x%p) to (0x%p)\n",
		     p_btif->rx_cb, rx_cb);
	}
	p_btif->rx_cb = rx_cb;

	return 0;
}

int btif_raise_wak_signal(p_mtk_btif p_btif)
{
	int i_ret = 0;
	P_MTK_BTIF_INFO_STR p_btif_info = p_btif->p_btif_info;

#if MTK_BTIF_ENABLE_CLK_REF_COUNTER
	hal_btif_clk_ctrl(p_btif->p_btif_info, CLK_OUT_ENABLE);
#endif

	i_ret = hal_btif_raise_wak_sig(p_btif_info);

#if MTK_BTIF_ENABLE_CLK_REF_COUNTER
	hal_btif_clk_ctrl(p_btif_info, CLK_OUT_DISABLE);
#endif
	return i_ret;
}

bool _btif_is_tx_complete(p_mtk_btif p_btif)
{
	bool b_ret = false;
	ENUM_BTIF_MODE tx_mode = p_btif->tx_mode;

/*
 * make sure BTIF tx finished in PIO mode
 * make sure BTIF tx finished and DMA tx finished in DMA mode
 */
	if (tx_mode == BTIF_MODE_DMA) {
		b_ret = hal_dma_is_tx_complete(p_btif->p_tx_dma->p_dma_info);
		if (b_ret == false) {
			BTIF_DBG_FUNC("Tx DMA is not finished\n");
			return b_ret;
		}
	}

	b_ret = hal_btif_is_tx_complete(p_btif->p_btif_info);
	if (b_ret == false) {
		BTIF_DBG_FUNC("BTIF Tx is not finished\n");
		return b_ret;
	}
	b_ret = true;
	return b_ret;
}

/*--------------------------------Functions-------------------------------------------*/

#if ENABLE_BTIF_TX_DMA
static int _btif_vfifo_init(p_mtk_btif_dma p_dma)
{
	P_DMA_VFIFO p_vfifo = NULL;
	struct device *dev = NULL;
	p_mtk_btif p_btif = NULL;

	if (p_dma == NULL) {
		BTIF_ERR_FUNC("p_dma is NULL\n");
		return E_BTIF_INVAL_PARAM;
	}

	p_btif = (p_mtk_btif)p_dma->p_btif;

	if (p_btif == NULL) {
		BTIF_ERR_FUNC("invalid parameter: p_btif(0x%p)\n", p_btif);
		return E_BTIF_INVAL_PARAM;
	}

	dev = (struct device *)p_btif->private_data;
	if (dev == NULL)
		BTIF_WARN_FUNC("Null dev pointer!!!!\n");

	p_vfifo = p_dma->p_dma_info->p_vfifo;
	if (p_vfifo->p_vir_addr != NULL) {
		BTIF_ERR_FUNC
		    ("BTIF vFIFO memory already allocated, do nothing\n");
		return E_BTIF_BAD_POINTER;
	}

/*vFIFO memory allocation*/
	p_vfifo->p_vir_addr = dma_zalloc_coherent(dev,
						  p_vfifo->vfifo_size,
						  &p_vfifo->phy_addr, GFP_DMA | GFP_DMA32);
	if (p_vfifo->p_vir_addr == NULL) {
		BTIF_ERR_FUNC("alloc vFIFO memory for BTIF failed\n");
		return E_BTIF_FAIL;
	}

	if (sizeof(dma_addr_t) == sizeof(unsigned long long))
		BTIF_INFO_FUNC("alloc vFIFO for BTIF succeed in arch64,vir addr:0x%p,",
		"phy addr:0x%llx\n", p_vfifo->p_vir_addr, p_vfifo->phy_addr);
	else
		BTIF_INFO_FUNC("alloc vFIFO for BTIF succeed in arch32,vir addr:0x%p,",
		"phy addr:0x%08x\n",	p_vfifo->p_vir_addr, p_vfifo->phy_addr);

	return 0;
}
#endif
#if ENABLE_BTIF_TX_DMA
static int _btif_vfifo_deinit(p_mtk_btif_dma p_dma)
{
	P_DMA_VFIFO p_vfifo = NULL;
	struct device *dev = NULL;
	p_mtk_btif p_btif = NULL;

	if (p_dma == NULL) {
		BTIF_ERR_FUNC("p_dma is NULL\n");
		return E_BTIF_INVAL_PARAM;
	}


	p_btif = (p_mtk_btif)p_dma->p_btif;
	if (p_btif == NULL) {
		BTIF_ERR_FUNC("invalid parameter: p_btif(0x%p)\n", p_btif);
		return E_BTIF_INVAL_PARAM;
	}

	dev = (struct device *)p_btif->private_data;
	if (dev == NULL)
		BTIF_WARN_FUNC("Null dev pointer!!!!\n");

	p_vfifo = p_dma->p_dma_info->p_vfifo;

/*free DMA memory if allocated successfully before*/
	if (p_vfifo->p_vir_addr != NULL) {
		dma_free_coherent(dev,
				  p_vfifo->vfifo_size,
				  p_vfifo->p_vir_addr, p_vfifo->phy_addr);
		p_vfifo->p_vir_addr = NULL;
	}

	return 0;
}
#endif

static int _btif_state_init(p_mtk_btif p_btif)
{
	if (p_btif == NULL) {
		BTIF_ERR_FUNC("p_btif is NULL\n");
		return E_BTIF_INVAL_PARAM;
	}
	p_btif->state = B_S_OFF;
	mutex_init(&(p_btif->state_mtx));

	return 0;
}

static int _btif_state_hold(p_mtk_btif p_btif)
{
	return mutex_lock_killable(&(p_btif->state_mtx));
}

static int _btif_state_set(p_mtk_btif p_btif, ENUM_BTIF_STATE state)
{
/*chaozhong: To do: need to finished state mechine here*/
	int i_ret = 0;
	int ori_state = p_btif->state;

	if (ori_state == state) {
		BTIF_INFO_FUNC("already in %s state\n", g_state[state]);
		return i_ret;
	}
	if ((state >= B_S_OFF) && (state < B_S_MAX)) {
		BTIF_DBG_FUNC("%s->%s request\n", g_state[ori_state],
			      g_state[state]);
		if (state == B_S_ON)
			_btif_dpidle_notify_ctrl(p_btif, BTIF_DPIDLE_DISABLE);
		switch (ori_state) {
		case B_S_ON:
/*B_S_ON can only be switched to B_S_OFF, B_S_SUSPEND and B_S_DPIDLE*/
/*B_S_ON->B_S_OFF : do nothing here*/
/*
 * B_S_ON->B_S_DPLE : disable clock backup
 * BTIF and DMA controller's register if necessary
 */
			if (state == B_S_DPIDLE) {
				/*clock controlled id done in _btif_enter_dpidle*/
				p_btif->state = state;
				i_ret = 0;
			} else if (state == B_S_OFF) {
				/*clock controlled is done in btif_close*/
				p_btif->state = state;
				i_ret = 0;
			} else if (state == B_S_SUSPEND) {
				/*clock controlled is done in btif_close*/
				p_btif->state = state;
				i_ret = 0;
			} else {
				BTIF_ERR_FUNC("%s->%s is not allowed\n",
					      g_state[ori_state],
					      g_state[state]);
				i_ret = E_BTIF_INVAL_STATE;
			}
			break;
		case B_S_DPIDLE:
/*B_S_DPIDLE can only be switched to B_S_ON and B_S_SUSPEND*/
/*B_S_DPIDLE-> B_S_ON: do nothing for this moment*/
/*
 * B_S_DPIDLE-> B_S_SUSPEND:
 * disable clock backup BTIF and DMA controller's register if necessary
 */
			if (state == B_S_ON) {
				/*clock controlled id done in _btif_exit_dpidle*/
				p_btif->state = state;
				i_ret = 0;
			} else if (state == B_S_SUSPEND) {
				/*clock controlled is done in _btif_exit_dpidle*/
				p_btif->state = state;
				i_ret = 0;
			} else {
				BTIF_ERR_FUNC("%s->%s is not allowed\n",
					      g_state[ori_state],
					      g_state[state]);
				i_ret = E_BTIF_INVAL_STATE;
			}
			break;

		case B_S_SUSPEND:
/*B_S_SUSPEND can be switched to B_S_IDLE and B_S_ON*/
/*reinit BTIF controller and DMA controller*/
			if (state == B_S_DPIDLE) {
				/*
				 * system call resume API, do resume operation,
				 * change to deepidle state
				 */
				p_btif->state = state;
				i_ret = 0;
			} else if (state == B_S_ON) {
				/*
				 * when stp want to send data before
				 * system do resume operation
				 */
				p_btif->state = state;
				i_ret = 0;
			} else {
				BTIF_ERR_FUNC("%s->%s is not allowed\n",
					      g_state[ori_state],
					      g_state[state]);
				i_ret = E_BTIF_INVAL_STATE;
			}
			break;

		case B_S_OFF:{
/*B_S_OFF can only be switched to B_S_ON*/
				if (state == B_S_ON) {
					/*clock controlled is done in btif_open*/
					p_btif->state = state;
					i_ret = 0;
				} else {
					BTIF_ERR_FUNC("%s->%s is not allowed\n",
						      g_state[ori_state],
						      g_state[state]);
					i_ret = E_BTIF_INVAL_STATE;
				}
			}
			break;
		default:
/*no this possibility*/
			BTIF_ERR_FUNC
			    ("state change request is not allowed, this should never happen\n");
			break;
		}

		if (state != B_S_ON)
			_btif_dpidle_notify_ctrl(p_btif, BTIF_DPIDLE_ENABLE);

	} else {
		i_ret = E_BTIF_INVAL_PARAM;
		BTIF_ERR_FUNC("invalid state:%d, do nothing\n", state);
	}
	return i_ret;
}

static ENUM_BTIF_STATE _btif_state_get(p_mtk_btif p_btif)
{
	return p_btif->state;
}

static int _btif_state_release(p_mtk_btif p_btif)
{
	int i_ret = 0;

	BTIF_MUTEX_UNLOCK(&(p_btif->state_mtx));
	return i_ret;
}

static int _btif_state_deinit(p_mtk_btif p_btif)
{
	if (p_btif == NULL) {
		BTIF_ERR_FUNC("p_btif is NULL\n");
		return E_BTIF_INVAL_PARAM;
	}
	p_btif->state = B_S_OFF;
	mutex_destroy(&(p_btif->state_mtx));

	return 0;
}

static int btif_rx_data_consummer(p_mtk_btif p_btif)
{
	unsigned int length = 0;
	unsigned char *p_buf = NULL;
/*get BTIF rx buffer's information*/
	p_btif_buf_str p_bbs = &(p_btif->btif_buf);
/*
 * wr_idx of btif_buf may be modified in IRQ handler,
 * in order not to be effected by case in which irq interrupt this operation,
 * we record wr_idx here
 */
	unsigned int wr_idx = p_bbs->wr_idx;

	length = BBS_COUNT_CUR(p_bbs, wr_idx);

/*make sure length of rx buffer data > 0*/
	do {
		if (length > 0) {
			/*
			 * check if rx_cb empty or not, if registered ,
			 * call user's rx callback to handle these data
			 */
			if (p_btif->rx_cb) {
				if (p_bbs->rd_idx <= wr_idx) {
					p_buf = BBS_PTR(p_bbs, p_bbs->rd_idx);
					/* p_buf = &(p_bbs->buf[p_bbs->rd_idx]); */
					/* length = BBS_COUNT(p_bbs); */
					length = (wr_idx >= (p_bbs)->rd_idx) ?
					    (wr_idx - (p_bbs)->rd_idx) :
					    BBS_SIZE(p_bbs) -
					    ((p_bbs)->rd_idx - wr_idx);
					if (p_btif->rx_cb)
						(*(p_btif->rx_cb)) (p_buf, length);
					else
						BTIF_ERR_FUNC("p_btif->rx_cb is NULL\n");
					/*update rx data read index*/
					p_bbs->rd_idx = wr_idx;
				} else {
					unsigned int len_tail =
					    BBS_SIZE(p_bbs) - (p_bbs)->rd_idx;
					/*p_buf = &(p_bbs->buf[p_bbs->->rd_idx]);*/
					p_buf = BBS_PTR(p_bbs, p_bbs->rd_idx);
					if (p_btif->rx_cb)
						(*(p_btif->rx_cb)) (p_buf, len_tail);
					else
						BTIF_ERR_FUNC("p_btif->rx_cb is NULL\n");
					length = BBS_COUNT_CUR(p_bbs, wr_idx);
					length -= len_tail;
					/*p_buf = &(p_bbs->buf[0]);*/
					p_buf = BBS_PTR(p_bbs, 0);
					if (p_btif->rx_cb)
						(*(p_btif->rx_cb)) (p_buf, length);
					else
						BTIF_ERR_FUNC("p_btif->rx_cb is NULL\n");
					/*update rx data read index*/
					p_bbs->rd_idx = wr_idx;
				}
			} else if (p_btif->rx_notify != NULL) {
				(*p_btif->rx_notify) ();
			} else {
				BTIF_WARN_FUNC
				    ("p_btif:0x%p, both rx_notify and rx_cb are NULL\n",
				     p_btif);
				break;
			}
		} else {
			BTIF_DBG_FUNC("length:%d\n", length);
			break;
		}
		wr_idx = p_bbs->wr_idx;
		length = BBS_COUNT_CUR(p_bbs, wr_idx);
	} while (1);
	return length;
}

#if BTIF_RXD_BE_BLOCKED_DETECT
static int mtk_btif_rxd_be_blocked_by_timer(void)
{
	int ret = 0;
	int counter = 0;
	unsigned int i;
	struct timeval now;
	int time_gap[MAX_BTIF_RXD_TIME_REC];

	do_gettimeofday(&now);

	for (i = 0; i < MAX_BTIF_RXD_TIME_REC; i++) {
		BTIF_INFO_FUNC("btif_rxd_time_stamp[%d]=%d.%d\n", i,
			btif_rxd_time_stamp[i].tv_sec, btif_rxd_time_stamp[i].tv_usec);
		if (now.tv_sec >= btif_rxd_time_stamp[i].tv_sec) {
			time_gap[i] = now.tv_sec - btif_rxd_time_stamp[i].tv_sec;
			time_gap[i] *= 1000000; /*second*/
			if (now.tv_usec >= btif_rxd_time_stamp[i].tv_usec)
				time_gap[i] += now.tv_usec - btif_rxd_time_stamp[i].tv_usec;
			else
				time_gap[i] += 1000000 - now.tv_usec + btif_rxd_time_stamp[i].tv_usec;

			if (time_gap[i] > 1000000)
				counter++;
			BTIF_INFO_FUNC("time_gap[%d]=%d,counter:%d\n", i, time_gap[i], counter);
		} else {
			time_gap[i] = 0;
			BTIF_ERR_FUNC("abnormal case now:%d < time_stamp[%d]:%d\n", now.tv_sec,
							i, btif_rxd_time_stamp[i].tv_usec);
		}
	}
	if (counter > (MAX_BTIF_RXD_TIME_REC - 2))
		ret = 1;
	return ret;
}
static int mtk_btif_rxd_be_blocked_by_data(void)
{
	unsigned int out_index = 0;
	unsigned int in_index = 0;
	unsigned int dump_size = 0;
	unsigned int len = 0;
	unsigned long flags;
	unsigned int sync_pkt_n = 0;
	P_BTIF_LOG_BUF_T p_log_buf = NULL;
	P_BTIF_LOG_QUEUE_T p_log_que = NULL;
	p_mtk_btif p_btif = &(g_btif[0]);

	p_log_que = &p_btif->rx_log;
	spin_lock_irqsave(&p_log_que->lock, flags);
	in_index = p_log_que->in;
	dump_size = p_log_que->size;
	out_index = p_log_que->size >=
	BTIF_LOG_ENTRY_NUM ? in_index : (BTIF_LOG_ENTRY_NUM -
					 p_log_que->size +
					 in_index) % BTIF_LOG_ENTRY_NUM;
	if (dump_size != 0) {
		while (dump_size--) {
			p_log_buf = p_log_que->p_queue[0] + out_index;
			len = p_log_buf->len;
			if (len > BTIF_LOG_SZ)
				len = BTIF_LOG_SZ;
			if ((0x7f == *(p_log_buf->buffer)) && (0x7f == *(p_log_buf->buffer + 1))) {
				sync_pkt_n++;
				BTIF_INFO_FUNC("tx pkt_count:%d is sync pkt\n", out_index);
			}
			out_index++;
			out_index %= BTIF_LOG_ENTRY_NUM;
		}
	}
	if (sync_pkt_n == 0)
		BTIF_ERR_FUNC("there is no sync pkt in BTIF buffer\n");
	else
		BTIF_ERR_FUNC("there are %d sync pkt in BTIF buffer\n", sync_pkt_n);
	spin_unlock_irqrestore(&p_log_que->lock, flags);
	return sync_pkt_n;
}

int mtk_btif_rxd_be_blocked_flag_get(void)
{
	int ret = 0;
	int condition1 = 0, condition2 = 0;

	condition1 = mtk_btif_rxd_be_blocked_by_timer();
	condition2 = mtk_btif_rxd_be_blocked_by_data();
	if (condition1 && condition2) {
		BTIF_ERR_FUNC("btif_rxd thread be blocked too long!\n");
		ret = 1;
	}
	return ret;
}
#endif
static int btif_rx_thread(void *p_data)
{
#if BTIF_RXD_BE_BLOCKED_DETECT
	unsigned int i = 0;
#endif
	p_mtk_btif p_btif = (p_mtk_btif)p_data;


	while (1) {
		wait_for_completion_interruptible(&p_btif->rx_comp);

		if (kthread_should_stop()) {
			BTIF_WARN_FUNC("btif rx thread stoping ...\n");
			break;
		}
#ifdef BTIF_RXD_BE_BLOCKED_DETECT
		do_gettimeofday(&btif_rxd_time_stamp[i]);
		i++;
		if (i >= MAX_BTIF_RXD_TIME_REC)
			i = 0;
#endif
		btif_rx_data_consummer(p_btif);
	}
	return 0;
}

static void btif_rx_worker(struct work_struct *p_work)
{
/*get mtk_btif's pointer*/
	p_mtk_btif p_btif = container_of(p_work, mtk_btif, rx_work);

	BTIF_DBG_FUNC("p_btif:0x%p\n", p_btif);
/*lock rx_mutex*/

	if (mutex_lock_killable(&(p_btif->rx_mtx))) {
		BTIF_ERR_FUNC("mutex_lock_killable return failed\n");
		return;
	}
	btif_rx_data_consummer(p_btif);
	BTIF_MUTEX_UNLOCK(&(p_btif->rx_mtx));
}

static void btif_tx_worker(struct work_struct *p_work)
{
	int i_ret = 0;
	int leng_sent = 0;
/*tx fifo out*/
	int how_much_get = 0;
	unsigned char local_buf[384];

/*get mtk_btif's pointer*/
	p_mtk_btif p_btif = container_of(p_work, mtk_btif, tx_work);

	BTIF_DBG_FUNC("p_btif:0x%p\n", p_btif);

	if (mutex_lock_killable(&(p_btif->tx_mtx))) {
		BTIF_ERR_FUNC("mutex_lock_killable return failed\n");
		return;
	}
	how_much_get =
	    kfifo_out(p_btif->p_tx_fifo, local_buf, sizeof(local_buf));
	do {
		while (leng_sent < how_much_get) {
			i_ret = _btif_send_data(p_btif,
						local_buf + leng_sent,
						how_much_get - leng_sent);
			if (i_ret > 0) {
				leng_sent += i_ret;
			} else if (i_ret == 0) {
				BTIF_WARN_FUNC
				    ("_btif_send_data return 0, retry\n");
			} else {
				BTIF_WARN_FUNC
				    ("btif send data fail,reset tx fifo, i_ret(%d)\n",
				     i_ret);
				kfifo_reset(p_btif->p_tx_fifo);
				break;
			}
		}
		how_much_get =
		    kfifo_out(p_btif->p_tx_fifo, local_buf, sizeof(local_buf));
		leng_sent = 0;
	} while (how_much_get > 0);
	BTIF_MUTEX_UNLOCK(&(p_btif->tx_mtx));
}

static void btif_rx_tasklet(unsigned long func_data)
{
	unsigned long flags;
/*get mtk_btif's pointer*/
	p_mtk_btif p_btif = (p_mtk_btif) func_data;

	BTIF_DBG_FUNC("p_btif:0x%p\n", p_btif);
/*lock rx_spinlock*/
	spin_lock_irqsave(&p_btif->rx_tasklet_spinlock, flags);
	btif_rx_data_consummer(p_btif);
	spin_unlock_irqrestore(&p_btif->rx_tasklet_spinlock, flags);
}

static int _btif_tx_ctx_init(p_mtk_btif p_btif)
{
	int i_ret = -1;

	if (p_btif == NULL) {
		BTIF_ERR_FUNC("p_btif is NULL\n");
		return E_BTIF_INVAL_PARAM;
	}

	if (p_btif->tx_ctx == BTIF_TX_SINGLE_CTX) {
		p_btif->p_tx_wq = create_singlethread_workqueue("btif_txd");

		if (!(p_btif->p_tx_wq)) {
			BTIF_ERR_FUNC
			    ("create_singlethread_workqueue for tx thread fail\n");
			i_ret = -ENOMEM;
			goto btm_init_err;
		}
		mutex_init(&(p_btif->tx_mtx));
/* init btif tx work */
		INIT_WORK(&(p_btif->tx_work), btif_tx_worker);
		BTIF_INFO_FUNC("btif_tx_worker init succeed\n");

		p_btif->p_tx_fifo = kzalloc(sizeof(struct kfifo), GFP_ATOMIC);
		if (p_btif->p_tx_fifo == NULL) {
			i_ret = -ENOMEM;
			BTIF_ERR_FUNC("kzalloc for p_btif->p_tx_fifo failed\n");
			goto btm_init_err;
		}

		i_ret = kfifo_alloc(p_btif->p_tx_fifo,
				    BTIF_TX_FIFO_SIZE, GFP_ATOMIC);
		if (i_ret != 0) {
			BTIF_ERR_FUNC("kfifo_alloc failed, errno(%d)\n", i_ret);
			i_ret = -ENOMEM;
			goto btm_init_err;
		}
	} else if (p_btif->tx_ctx == BTIF_TX_USER_CTX) {
		BTIF_INFO_FUNC
		    ("nothing is done when btif tx in user's thread\n");
	} else {
		BTIF_ERR_FUNC("unsupported tx context type:%d\n",
			      p_btif->tx_ctx);
		goto btm_init_err;
	}

	BTIF_INFO_FUNC("succeed\n");

	i_ret = 0;
	return i_ret;
btm_init_err:
	if (p_btif->tx_ctx == BTIF_TX_SINGLE_CTX) {
		if (p_btif->p_tx_wq) {
			destroy_workqueue(p_btif->p_tx_wq);
			p_btif->p_tx_wq = NULL;
			BTIF_INFO_FUNC("btif_tx_workqueue destroyed\n");
		}
		kfree(p_btif->p_tx_fifo);
	}
	return i_ret;
}

static int _btif_tx_ctx_deinit(p_mtk_btif p_btif)
{
	int i_ret = 0;

	if (p_btif->tx_ctx == BTIF_TX_SINGLE_CTX) {
		if (p_btif->p_tx_wq) {
			destroy_workqueue(p_btif->p_tx_wq);
			p_btif->p_tx_wq = NULL;
			BTIF_INFO_FUNC("btif_tx_workqueue destroyed\n");
		}
		if (p_btif->p_tx_fifo) {
			kfifo_free(p_btif->p_tx_fifo);
			kfree(p_btif->p_tx_fifo);
			p_btif->p_tx_fifo = NULL;
		}
	}
	return i_ret;
}

static int _btif_rx_btm_init(p_mtk_btif p_btif)
{
	int i_ret = -1;

	if (p_btif == NULL) {
		BTIF_ERR_FUNC("p_btif is NULL\n");
		return E_BTIF_INVAL_PARAM;
	}

	if (p_btif->btm_type == BTIF_THREAD_CTX) {
		init_completion(&p_btif->rx_comp);

		/*create kernel thread for later rx data handle*/
		p_btif->p_task = kthread_create(btif_rx_thread, p_btif, "btif_rxd");
		if (p_btif->p_task == NULL) {
			BTIF_ERR_FUNC("kthread_create fail\n");
			i_ret = -ENOMEM;
			goto btm_init_err;
		}

#if ENABLE_BTIF_RX_THREAD_RT_SCHED
		{
			int i_ret = -1;
			int policy = SCHED_FIFO;
			struct sched_param param;

			param.sched_priority = MAX_RT_PRIO - 20;
			i_ret = sched_setscheduler(p_btif->p_task, policy, &param);
			if (i_ret != 0)
				BTIF_WARN_FUNC("set RT to btif_rxd workqueue failed\n");
			else
				BTIF_INFO_FUNC("set RT to btif_rxd workqueue succeed\n");
		}
#endif

		wake_up_process(p_btif->p_task);
		BTIF_INFO_FUNC("btif_rxd start to work!\n");
	} else if (p_btif->btm_type == BTIF_WQ_CTX) {
		p_btif->p_rx_wq = create_singlethread_workqueue("btif_rxwq");
		if (!(p_btif->p_rx_wq)) {
			BTIF_ERR_FUNC("create_singlethread_workqueue fail\n");
			i_ret = -ENOMEM;
			goto btm_init_err;
		}
		mutex_init(&(p_btif->rx_mtx));
		/* init btif rx work */
		INIT_WORK(&(p_btif->rx_work), btif_rx_worker);
		BTIF_INFO_FUNC("btif_rx_worker init succeed\n");
	} else if (p_btif->btm_type == BTIF_TASKLET_CTX) {
		/*init rx tasklet*/
		tasklet_init(&(p_btif->rx_tasklet), btif_rx_tasklet,
			     (unsigned long)p_btif);
		spin_lock_init(&(p_btif->rx_tasklet_spinlock));
		BTIF_INFO_FUNC("btif_rx_tasklet init succeed\n");
	} else {
		BTIF_ERR_FUNC("unsupported rx context type:%d\n",
			      p_btif->btm_type);
	}

/*spinlock init*/
	spin_lock_init(&(p_btif->rx_irq_spinlock));
	BTIF_INFO_FUNC("rx_spin_lock init succeed\n");

	i_ret = 0;
	return i_ret;
btm_init_err:
	if (p_btif->btm_type == BTIF_THREAD_CTX) {
		/*do nothing*/
		BTIF_INFO_FUNC("failed\n");
	} else if (p_btif->btm_type == BTIF_WQ_CTX) {
		if (p_btif->p_rx_wq) {
			destroy_workqueue(p_btif->p_rx_wq);
			p_btif->p_rx_wq = NULL;
			BTIF_INFO_FUNC("btif_rx_workqueue destroyed\n");
		}
	}
	return i_ret;
}

static int _btif_rx_btm_sched(p_mtk_btif p_btif)
{
	if (p_btif == NULL) {
		BTIF_ERR_FUNC("p_btif is NULL\n");
		return E_BTIF_INVAL_PARAM;
	}

	if (p_btif->btm_type == BTIF_THREAD_CTX) {
		complete(&p_btif->rx_comp);
		BTIF_DBG_FUNC("schedule btif_rx_thread\n");
	} else if (p_btif->btm_type == BTIF_WQ_CTX) {
		queue_work(p_btif->p_rx_wq, &(p_btif->rx_work));
		BTIF_DBG_FUNC("schedule btif_rx_worker\n");
	} else if (p_btif->btm_type == BTIF_TASKLET_CTX) {
		/*schedule it!*/
		tasklet_schedule(&(p_btif->rx_tasklet));
		BTIF_DBG_FUNC("schedule btif_rx_tasklet\n");
	} else {
		BTIF_ERR_FUNC("unsupported rx context type:%d\n",
			      p_btif->btm_type);
	}

	return 0;
}

static int _btif_rx_btm_deinit(p_mtk_btif p_btif)
{
	if (p_btif == NULL) {
		BTIF_ERR_FUNC("p_btif is NULL\n");
		return E_BTIF_INVAL_PARAM;
	}
	if (p_btif->btm_type == BTIF_THREAD_CTX) {
		if (p_btif->p_task != NULL) {
			BTIF_INFO_FUNC("signaling btif rx thread to stop ...\n");
			kthread_stop(p_btif->p_task);
		}
	} else if (p_btif->btm_type == BTIF_WQ_CTX) {
		if (p_btif->p_rx_wq) {
			cancel_work_sync(&(p_btif->rx_work));
			BTIF_INFO_FUNC("btif_rx_worker cancelled\n");
			destroy_workqueue(p_btif->p_rx_wq);
			p_btif->p_rx_wq = NULL;
			BTIF_INFO_FUNC("btif_rx_workqueue destroyed\n");
		}
		mutex_destroy(&(p_btif->rx_mtx));
	} else if (p_btif->btm_type == BTIF_TASKLET_CTX) {
		tasklet_kill(&(p_btif->rx_tasklet));
		BTIF_INFO_FUNC("rx_tasklet killed\n");
	} else {
		BTIF_ERR_FUNC("unsupported rx context type:%d\n",
			      p_btif->btm_type);
	}

	spin_lock_init(&(p_btif->rx_irq_spinlock));

	return 0;
}


void btif_dump_bbs_str(unsigned char *p_str, p_btif_buf_str p_bbs)
{
	BTIF_INFO_FUNC
	    ("%s UBS:0x%p\n  Size:0x%p\n  read:0x%08x\n  write:0x%08x\n",
	     p_str, p_bbs, p_bbs->size, p_bbs->rd_idx, p_bbs->wr_idx);
}

unsigned int btif_bbs_write(p_btif_buf_str p_bbs,
			    unsigned char *p_buf, unsigned int buf_len)
{
/*in IRQ context, so read operation won't interrupt this operation*/

	unsigned int wr_len = 0;

	unsigned int emp_len = BBS_LEFT(p_bbs);
	unsigned int ava_len = emp_len - 1;
	p_mtk_btif p_btif = container_of(p_bbs, mtk_btif, btif_buf);

	if (ava_len <= 0) {
		BTIF_ERR_FUNC
		    ("no empty space left for write, (%d)ava_len, (%d)to write\n",
		     ava_len, buf_len);
		hal_btif_dump_reg(p_btif->p_btif_info, REG_BTIF_ALL);
		hal_dma_dump_reg(p_btif->p_rx_dma->p_dma_info, REG_RX_DMA_ALL);
		return 0;
	}

	if (ava_len < buf_len) {
		BTIF_ERR_FUNC("BTIF overrun, (%d)empty, (%d)needed\n",
			      emp_len, buf_len);
		hal_btif_dump_reg(p_btif->p_btif_info, REG_BTIF_ALL);
		hal_dma_dump_reg(p_btif->p_rx_dma->p_dma_info, REG_RX_DMA_ALL);
		_btif_dump_memory("<DMA Rx vFIFO>", p_buf, buf_len);
	}

	if (buf_len >= g_max_pkg_len) {
		BTIF_WARN_FUNC("buf_len too long, (%d)ava_len, (%d)to write\n",
			       ava_len, buf_len);
		hal_btif_dump_reg(p_btif->p_btif_info, REG_BTIF_ALL);
		hal_dma_dump_reg(p_btif->p_rx_dma->p_dma_info, REG_RX_DMA_ALL);
		_btif_dump_memory("<DMA Rx vFIFO>", p_buf, buf_len);
	}

	wr_len = min(buf_len, ava_len);
	btif_bbs_wr_direct(p_bbs, p_buf, wr_len);

	if (BBS_COUNT(p_bbs) >= g_max_pding_data_size) {
		BTIF_WARN_FUNC("Rx buf_len too long, size(%d)\n",
			       BBS_COUNT(p_bbs));
		btif_dump_bbs_str("Rx buffer tooo long", p_bbs);
		hal_btif_dump_reg(p_btif->p_btif_info, REG_BTIF_ALL);
		hal_dma_dump_reg(p_btif->p_rx_dma->p_dma_info, REG_RX_DMA_ALL);
		_btif_dump_memory("<DMA Rx vFIFO>", p_buf, buf_len);
		BBS_INIT(p_bbs);
	}

	return wr_len;
}

unsigned int btif_bbs_read(p_btif_buf_str p_bbs,
			   unsigned char *p_buf, unsigned int buf_len)
{
	unsigned int rd_len = 0;
	unsigned int ava_len = 0;
	unsigned int wr_idx = p_bbs->wr_idx;

	ava_len = BBS_COUNT_CUR(p_bbs, wr_idx);
	if (ava_len >= 4096) {
		BTIF_WARN_FUNC("ava_len too long, size(%d)\n", ava_len);
		btif_dump_bbs_str("Rx buffer tooo long", p_bbs);
	}
	if (ava_len != 0) {
		if (buf_len >= ava_len) {
			rd_len = ava_len;
			if (wr_idx >= (p_bbs)->rd_idx) {
				memcpy(p_buf, BBS_PTR(p_bbs,
						      p_bbs->rd_idx),
				       ava_len);
				(p_bbs)->rd_idx = wr_idx;
			} else {
				unsigned int tail_len = BBS_SIZE(p_bbs) -
				    (p_bbs)->rd_idx;
				memcpy(p_buf, BBS_PTR(p_bbs,
						      p_bbs->rd_idx),
				       tail_len);
				memcpy(p_buf + tail_len, BBS_PTR(p_bbs,
								 0), ava_len - tail_len);
				(p_bbs)->rd_idx = wr_idx;
			}
		} else {
			rd_len = buf_len;
			if (wr_idx >= (p_bbs)->rd_idx) {
				memcpy(p_buf, BBS_PTR(p_bbs,
						      p_bbs->rd_idx),
				       rd_len);
				(p_bbs)->rd_idx = (p_bbs)->rd_idx + rd_len;
			} else {
				unsigned int tail_len = BBS_SIZE(p_bbs) -
				    (p_bbs)->rd_idx;
				if (tail_len >= rd_len) {
					memcpy(p_buf, BBS_PTR(p_bbs, p_bbs->rd_idx),
					       rd_len);
					(p_bbs)->rd_idx =
					    ((p_bbs)->rd_idx + rd_len) & (BBS_MASK(p_bbs));
				} else {
					memcpy(p_buf, BBS_PTR(p_bbs, p_bbs->rd_idx), tail_len);
					memcpy(p_buf + tail_len,
					       (p_bbs)->p_buf, rd_len - tail_len);
					(p_bbs)->rd_idx = rd_len - tail_len;
				}
			}
		}
	}
	mb();
	return rd_len;
}

unsigned int btif_bbs_wr_direct(p_btif_buf_str p_bbs,
				unsigned char *p_buf, unsigned int buf_len)
{
	unsigned int tail_len = 0;
	unsigned int l = 0;
	unsigned int tmp_wr_idx = p_bbs->wr_idx;

	tail_len = BBS_SIZE(p_bbs) - (tmp_wr_idx & BBS_MASK(p_bbs));

	l = min(tail_len, buf_len);

	memcpy((p_bbs->p_buf) + (tmp_wr_idx & BBS_MASK(p_bbs)), p_buf, l);
	memcpy(p_bbs->p_buf, p_buf + l, buf_len - l);

	mb();

	tmp_wr_idx += buf_len;
	tmp_wr_idx &= BBS_MASK(p_bbs);
	p_bbs->wr_idx = tmp_wr_idx;

	mb();
	return buf_len;
}

int _btif_dma_write(p_mtk_btif p_btif,
		    const unsigned char *p_buf, unsigned int buf_len)
{
	unsigned int i_ret = 0;
	unsigned int retry = 0;
	unsigned int max_tx_retry = 10;

	P_MTK_DMA_INFO_STR p_dma_info = p_btif->p_tx_dma->p_dma_info;

	_btif_irq_ctrl_sync(p_dma_info->p_irq, false);
	do {
		/*wait until tx is allowed*/
		while (!hal_dma_is_tx_allow(p_dma_info) &&
		       (retry < max_tx_retry)) {
			retry++;
			if (retry >= max_tx_retry) {
				BTIF_ERR_FUNC("wait for tx allowed timeout\n");
				break;
			}
		}
		if (retry >= max_tx_retry)
			break;

		if (buf_len <= hal_dma_get_ava_room(p_dma_info))
			i_ret = hal_dma_send_data(p_dma_info, p_buf, buf_len);
		else
			i_ret = 0;
	} while (0);
	_btif_irq_ctrl_sync(p_dma_info->p_irq, true);
	return i_ret;
}

int _btif_pio_write(p_mtk_btif p_btif,
		    const unsigned char *p_buf, unsigned int buf_len)
{
	unsigned int i_ret = 0;
	unsigned int sent_len = 0;
	unsigned int retry = 0;
	unsigned int max_tx_retry = 10;
	P_MTK_BTIF_INFO_STR p_btif_info = p_btif->p_btif_info;

	while ((sent_len < buf_len)) {
		if (hal_btif_is_tx_allow(p_btif_info)) {
			i_ret = hal_btif_send_data(p_btif_info,
						   p_buf + sent_len,
						   buf_len - sent_len);
			if (i_ret > 0) {
				sent_len += i_ret;
				BTIF_DBG_FUNC("lent sent:%d, total sent:%d\n",
					      i_ret, sent_len);
				retry = 0;
			}
		}
		if ((++retry > max_tx_retry) || (i_ret < 0)) {
			BTIF_INFO_FUNC("exceed retry times limit :%d\n", retry);
			break;
		}
	}
	i_ret = sent_len;
	return i_ret;
}

int _btif_dump_memory(char *str, unsigned char *p_buf, unsigned int buf_len)
{
	unsigned int idx = 0;

	pr_debug("%s:, length:%d\n", str, buf_len);
	for (idx = 0; idx < buf_len;) {
		pr_debug("%02x ", p_buf[idx]);
		idx++;
		if (idx % 8 == 0)
			pr_debug("\n");
	}
	return 0;
}

int btif_send_data(p_mtk_btif p_btif,
		   const unsigned char *p_buf, unsigned int buf_len)
{
	int i_ret = 0;

	if (p_btif->tx_ctx == BTIF_TX_USER_CTX) {
		i_ret = _btif_send_data(p_btif, p_buf, buf_len);
	} else if (p_btif->tx_ctx == BTIF_TX_SINGLE_CTX) {
		int length = 0;
/*tx fifo in*/
		length = kfifo_in(p_btif->p_tx_fifo,
				  (unsigned char *)p_buf, buf_len);
		if (length == buf_len) {
			queue_work(p_btif->p_tx_wq, &(p_btif->tx_work));
			BTIF_DBG_FUNC("schedule btif_tx_worker\n");
			i_ret = length;
		} else {
			i_ret = 0;
			BTIF_ERR_FUNC("fifo in failed, target len(%d),in len(%d),",
				"don't schedule btif_tx_worker\n", buf_len, length);
		}
	} else {
		BTIF_ERR_FUNC("invalid btif tx context:%d\n", p_btif->tx_ctx);
		i_ret = 0;
	}

	return i_ret;
}

int _btif_send_data(p_mtk_btif p_btif,
		    const unsigned char *p_buf, unsigned int buf_len)
{
	int i_ret = 0;
	unsigned int state = 0;

/*make sure BTIF in ON state before doing tx operation*/
	if (_btif_state_hold(p_btif))
		return E_BTIF_INTR;
	state = _btif_state_get(p_btif);

	if (state != B_S_ON)
		i_ret = _btif_exit_dpidle(p_btif);

	if (i_ret != 0) {
		i_ret = E_BTIF_INVAL_STATE;
	} else if (p_btif->tx_mode == BTIF_MODE_DMA) {
		/*_btif_dump_memory("tx data:", p_buf, buf_len);*/
		i_ret = _btif_dma_write(p_btif, p_buf, buf_len);
	} else if (p_btif->tx_mode == BTIF_MODE_PIO) {
		/*_btif_dump_memory("tx data:", p_buf, buf_len);*/
		i_ret = _btif_pio_write(p_btif, p_buf, buf_len);
	} else {
		BTIF_ERR_FUNC("invalid tx mode:%d\n", p_btif->tx_mode);
		i_ret = 0;
	}

/*save Tx packet here*/
	if (i_ret > 0)
		btif_log_buf_dmp_in(&p_btif->tx_log, p_buf, i_ret);

	BTIF_STATE_RELEASE(p_btif);
	return i_ret;
}

int btif_dump_reg(p_mtk_btif p_btif)
{
	int i_ret = 0;
	unsigned int ori_state = 0;

/*make sure BTIF in ON state before doing tx operation*/
	if (_btif_state_hold(p_btif))
		return E_BTIF_INTR;
	ori_state = _btif_state_get(p_btif);

	if (ori_state == B_S_OFF) {
		i_ret = E_BTIF_INVAL_STATE;
		BTIF_ERR_FUNC
		    ("BTIF in OFF state, ",
		     "should no need to dump register, ",
		     "please check wmt's operation is okay or not.\n");
		goto dmp_reg_err;
	}

	if ((ori_state != B_S_ON) && (ori_state < B_S_MAX)) {
		BTIF_ERR_FUNC("BTIF's original state is %s, not B_S_ON\n", g_state[ori_state]);
		BTIF_ERR_FUNC("!!!!---<<<This should never happen in normal mode>>>---!!!");
		i_ret = _btif_exit_dpidle(p_btif);
	}

	if (i_ret != 0) {
		i_ret = E_BTIF_INVAL_STATE;
		BTIF_ERR_FUNC("switch to B_S_ON failed\n");
		goto dmp_reg_err;
	}

/*dump BTIF register*/
	hal_btif_dump_reg(p_btif->p_btif_info, REG_BTIF_ALL);

/*dump BTIF Tx DMA channel register if in DMA mode*/
	if (p_btif->tx_mode == BTIF_MODE_DMA)
		hal_dma_dump_reg(p_btif->p_tx_dma->p_dma_info, REG_TX_DMA_ALL);
	else
		BTIF_INFO_FUNC("BTIF Tx in PIO mode,no need to dump Tx DMA's register\n");

/*dump BTIF Rx DMA channel register if in DMA mode*/
	if (p_btif->rx_mode == BTIF_MODE_DMA)
		hal_dma_dump_reg(p_btif->p_rx_dma->p_dma_info, REG_RX_DMA_ALL);
	else
		BTIF_INFO_FUNC("BTIF Rx in PIO mode,no need to dump Rx DMA's register\n");

	switch (ori_state) {
	case B_S_SUSPEND:
/*return to dpidle state*/
/* break; */
	case B_S_DPIDLE:
/*return to dpidle state*/
		_btif_enter_dpidle(p_btif);
		break;
	case B_S_ON:
/*nothing needs to be done*/
		break;
	default:
		break;
	}

dmp_reg_err:

	BTIF_STATE_RELEASE(p_btif);
	return i_ret;
}

int btif_rx_notify_reg(p_mtk_btif p_btif, MTK_BTIF_RX_NOTIFY rx_notify)
{
	if (p_btif->rx_notify) {
		BTIF_WARN_FUNC
		    ("rx cb already exist, rewrite from (0x%p) to (0x%p)\n",
		     p_btif->rx_notify, rx_notify);
	}
	p_btif->rx_notify = rx_notify;

	return 0;
}

int btif_dump_data(char *p_buf, int len)
{
	unsigned int idx = 0;
	unsigned char str[30];
	unsigned char *p_str;

	p_str = &str[0];
	for (idx = 0; idx < len; idx++, p_buf++) {
		sprintf(p_str, "%02x ", *p_buf);
		p_str += 3;
		if (7 == (idx % 8)) {
			*p_str++ = '\n';
			*p_str = '\0';
			pr_debug("%s", str);
			p_str = &str[0];
		}
	}
	if (len % 8) {
		*p_str++ = '\n';
		*p_str = '\0';
		pr_debug("%s", str);
	}
	return 0;
}

int btif_log_buf_dmp_in(P_BTIF_LOG_QUEUE_T p_log_que, const char *p_buf,
			int len)
{
	P_BTIF_LOG_BUF_T p_log_buf = NULL;
	char *dir = NULL;
	struct timeval *p_timer = NULL;
	unsigned long flags;
	bool output_flag = false;

	BTIF_DBG_FUNC("++\n");

	if ((p_log_que == NULL) || (p_buf == NULL) || (len == 0)) {
		BTIF_ERR_FUNC("invalid parameter, p_log_que(0x%x), buf(0x%x), ",
			"len(%d)\n", p_log_que, p_buf, len);
		return 0;
	}
	if (!(p_log_que->enable))
		return 0;

	dir = p_log_que->dir == BTIF_TX ? "Tx" : "Rx";
	output_flag = p_log_que->output_flag;

	spin_lock_irqsave(&(p_log_que->lock), flags);

/*get next log buffer for record usage*/
	p_log_buf = p_log_que->p_queue[0] + p_log_que->in;
	p_timer = &p_log_buf->timer;

/*log time stamp*/
	do_gettimeofday(p_timer);

/*record data information including length and content*/
	p_log_buf->len = len;
	memcpy(p_log_buf->buffer, p_buf, len > BTIF_LOG_SZ ? BTIF_LOG_SZ : len);

/*update log queue size information*/
	p_log_que->size++;
	p_log_que->size = p_log_que->size >
	    BTIF_LOG_ENTRY_NUM ? BTIF_LOG_ENTRY_NUM : p_log_que->size;

/*update log queue index information*/
	p_log_que->in++;
	p_log_que->in %= BTIF_LOG_ENTRY_NUM;

	spin_unlock_irqrestore(&p_log_que->lock, flags);

/*check if log dynamic output function is enabled or not*/
	if (output_flag) {
		pr_debug("BTIF-DBG, dir:%s, %d.%ds len:%d\n",
		       dir, (int)p_timer->tv_sec, (int)p_timer->tv_usec, len);
/*output buffer content*/
		btif_dump_data((char *)p_buf, len);
	}
	BTIF_DBG_FUNC("--\n");

	return 0;
}

int btif_log_buf_dmp_out(P_BTIF_LOG_QUEUE_T p_log_que)
{
	P_BTIF_LOG_BUF_T p_log_buf = NULL;
	unsigned int out_index = 0;
	unsigned int in_index = 0;
	unsigned int dump_size = 0;
	unsigned char *p_buf = NULL;
	unsigned int len = 0;
	unsigned int pkt_count = 0;
	unsigned char *p_dir = NULL;
	struct timeval *p_timer = NULL;
	unsigned long flags;

#if 0				/* no matter enable or not, we allowed output */
	if (!(p_log_que->enable))
		return;
#endif
	BTIF_DBG_FUNC("++\n");

	spin_lock_irqsave(&p_log_que->lock, flags);
	in_index = p_log_que->in;
	dump_size = p_log_que->size;
	out_index = p_log_que->size >=
	    BTIF_LOG_ENTRY_NUM ? in_index : (BTIF_LOG_ENTRY_NUM -
					     p_log_que->size +
					     in_index) % BTIF_LOG_ENTRY_NUM;
	p_dir = p_log_que->dir == BTIF_TX ? "Tx" : "Rx";

	BTIF_INFO_FUNC("btif %s log buffer size:%d\n", p_dir, dump_size);

	if (dump_size != 0) {
		while (dump_size--) {
			p_log_buf = p_log_que->p_queue[0] + out_index;

			len = p_log_buf->len;
			p_buf = p_log_buf->buffer;
			p_timer = &p_log_buf->timer;

			len = len > BTIF_LOG_SZ ? BTIF_LOG_SZ : len;

			BTIF_INFO_FUNC("dir:%s, pkt_count:%d, %d.%ds len:%d\n",
			       p_dir,
			       pkt_count++,
			       (int)p_timer->tv_sec,
			       (int)p_timer->tv_usec, len);
/*output buffer content*/
			btif_dump_data(p_log_buf->buffer, len);
			out_index++;
			out_index %= BTIF_LOG_ENTRY_NUM;
		}
	}
	spin_unlock_irqrestore(&p_log_que->lock, flags);
	BTIF_DBG_FUNC("--\n");

	return 0;
}

int btif_log_buf_enable(P_BTIF_LOG_QUEUE_T p_log_que)
{
	unsigned long flags;

	spin_lock_irqsave(&p_log_que->lock, flags);
	p_log_que->enable = true;
	spin_unlock_irqrestore(&p_log_que->lock, flags);
	BTIF_INFO_FUNC("enable %s log function\n",
		       p_log_que->dir == BTIF_TX ? "Tx" : "Rx");
	return 0;
}

int btif_log_buf_disable(P_BTIF_LOG_QUEUE_T p_log_que)
{
	unsigned long flags;

	spin_lock_irqsave(&p_log_que->lock, flags);
	p_log_que->enable = false;
	spin_unlock_irqrestore(&p_log_que->lock, flags);
	BTIF_INFO_FUNC("disable %s log function\n",
		       p_log_que->dir == BTIF_TX ? "Tx" : "Rx");
	return 0;
}

int btif_log_output_enable(P_BTIF_LOG_QUEUE_T p_log_que)
{
	unsigned long flags;

	spin_lock_irqsave(&p_log_que->lock, flags);
	p_log_que->output_flag = true;
	spin_unlock_irqrestore(&p_log_que->lock, flags);
	BTIF_INFO_FUNC("%s log rt output enabled\n",
		       p_log_que->dir == BTIF_TX ? "Tx" : "Rx");
	return 0;
}

int btif_log_output_disable(P_BTIF_LOG_QUEUE_T p_log_que)
{
	unsigned long flags;

	spin_lock_irqsave(&p_log_que->lock, flags);
	p_log_que->output_flag = false;
	spin_unlock_irqrestore(&p_log_que->lock, flags);
	BTIF_INFO_FUNC("%s log rt output disabled\n",
		       p_log_que->dir == BTIF_TX ? "Tx" : "Rx");
	return 0;
}

int btif_log_buf_reset(P_BTIF_LOG_QUEUE_T p_log_que)
{
	unsigned long flags;

	spin_lock_irqsave(&p_log_que->lock, flags);

/*tx log buffer init*/
	p_log_que->in = 0;
	p_log_que->out = 0;
	p_log_que->size = 0;
	p_log_que->enable = true;
	memset((p_log_que->p_queue[0]), 0, sizeof(BTIF_LOG_BUF_T));

	spin_unlock_irqrestore(&p_log_que->lock, flags);
	BTIF_DBG_FUNC("reset %s log buffer\n",
		       p_log_que->dir == BTIF_TX ? "Tx" : "Rx");
	return 0;
}

int btif_log_buf_init(p_mtk_btif p_btif)
{
/*tx log buffer init*/
	p_btif->tx_log.dir = BTIF_TX;
	p_btif->tx_log.in = 0;
	p_btif->tx_log.out = 0;
	p_btif->tx_log.size = 0;
	p_btif->tx_log.output_flag = false;
	p_btif->tx_log.enable = true;
	spin_lock_init(&(p_btif->tx_log.lock));
	BTIF_DBG_FUNC("tx_log.p_queue:0x%p\n", p_btif->tx_log.p_queue[0]);
	memset((p_btif->tx_log.p_queue[0]), 0, sizeof(BTIF_LOG_BUF_T));

/*rx log buffer init*/
	p_btif->rx_log.dir = BTIF_RX;
	p_btif->rx_log.in = 0;
	p_btif->rx_log.out = 0;
	p_btif->rx_log.size = 0;
	p_btif->rx_log.output_flag = false;
	p_btif->rx_log.enable = true;
	spin_lock_init(&(p_btif->rx_log.lock));
	BTIF_DBG_FUNC("rx_log.p_queue:0x%p\n", p_btif->rx_log.p_queue[0]);
	memset((p_btif->rx_log.p_queue[0]), 0, sizeof(BTIF_LOG_BUF_T));

	return 0;
}

int btif_tx_dma_mode_set(int en)
{
	int index = 0;
	ENUM_BTIF_MODE mode = (en == 1) ? BTIF_MODE_DMA : BTIF_MODE_PIO;

	for (index = 0; index < BTIF_PORT_NR; index++)
		g_btif[index].tx_mode = mode;

	return 0;
}

int btif_rx_dma_mode_set(int en)
{
	int index = 0;
	ENUM_BTIF_MODE mode = (en == 1) ? BTIF_MODE_DMA : BTIF_MODE_PIO;

	for (index = 0; index < BTIF_PORT_NR; index++)
		g_btif[index].rx_mode = mode;

	return 0;
}

static int BTIF_init(void)
{
	int i_ret = -1;
	int index = 0;
	p_mtk_btif_dma p_tx_dma = NULL;
	p_mtk_btif_dma p_rx_dma = NULL;
	unsigned char *p_btif_buffer = NULL;
	unsigned char *p_tx_queue = NULL;
	unsigned char *p_rx_queue = NULL;

	BTIF_DBG_FUNC("++\n");

/*Platform Driver initialization*/
	i_ret = platform_driver_register(&mtk_btif_dev_drv);
	if (i_ret) {
		BTIF_ERR_FUNC("BTIF platform driver registered failed, ret(%d)\n", i_ret);
		goto err_exit1;
	}

	i_ret = driver_create_file(&mtk_btif_dev_drv.driver, &driver_attr_flag);
	if (i_ret)
		BTIF_ERR_FUNC("BTIF pdriver_create_file failed, ret(%d)\n", i_ret);

/*SW init*/
	for (index = 0; index < BTIF_PORT_NR; index++) {
		p_btif_buffer = kmalloc(BTIF_RX_BUFFER_SIZE, GFP_ATOMIC);
		if (!p_btif_buffer) {
			BTIF_ERR_FUNC("p_btif_buffer kmalloc memory fail\n");
			return -1;
		}
		BTIF_INFO_FUNC("p_btif_buffer get memory 0x%p\n", p_btif_buffer);
		p_tx_queue = kmalloc_array(BTIF_LOG_ENTRY_NUM, sizeof(BTIF_LOG_BUF_T), GFP_ATOMIC);
		if (!p_tx_queue) {
			BTIF_ERR_FUNC("p_tx_queue kmalloc memory fail\n");
			kfree(p_btif_buffer);
			return -1;
		}
		BTIF_INFO_FUNC("p_tx_queue get memory 0x%p\n", p_tx_queue);
		p_rx_queue = kmalloc_array(BTIF_LOG_ENTRY_NUM, sizeof(BTIF_LOG_BUF_T), GFP_ATOMIC);
		if (!p_rx_queue) {
			BTIF_ERR_FUNC("p_rx_queue kmalloc memory fail\n");
			kfree(p_btif_buffer);
			kfree(p_tx_queue);
			return -1;
		}
		BTIF_INFO_FUNC("p_rx_queue get memory 0x%p\n", p_rx_queue);

		INIT_LIST_HEAD(&(g_btif[index].user_list));
		BBS_INIT(&(g_btif[index].btif_buf));
		g_btif[index].enable = false;
		g_btif[index].open_counter = 0;
		g_btif[index].setting = &g_btif_setting[index];
		g_btif[index].p_btif_info = hal_btif_info_get();
		g_btif[index].tx_mode = g_btif_setting[index].tx_mode;
		g_btif[index].rx_mode = g_btif_setting[index].rx_mode;
		g_btif[index].btm_type = g_btif_setting[index].rx_type;
		g_btif[index].tx_ctx = g_btif_setting[index].tx_type;
		g_btif[index].lpbk_flag = false;
		g_btif[index].rx_cb = NULL;
		g_btif[index].rx_notify = NULL;
		g_btif[index].btif_buf.p_buf = p_btif_buffer;
		g_btif[index].tx_log.p_queue[0] = (P_BTIF_LOG_BUF_T) p_tx_queue;
		g_btif[index].rx_log.p_queue[0] = (P_BTIF_LOG_BUF_T) p_rx_queue;
		btif_log_buf_init(&g_btif[index]);

#if !(MTK_BTIF_ENABLE_CLK_REF_COUNTER)
/*enable BTIF clock gating by default*/
		i_ret = hal_btif_clk_ctrl(g_btif[index].p_btif_info,
					  CLK_OUT_DISABLE);
		if (i_ret != 0) {
			BTIF_ERR_FUNC("BTIF controller CG failed\n");
			goto err_exit2;
		}
#endif

/*
 * viftual FIFO memory must be physical continious,
 * because DMA will access it directly without MMU
 */
#if ENABLE_BTIF_TX_DMA
		p_tx_dma = &g_dma[index][BTIF_TX];
		g_btif[index].p_tx_dma = p_tx_dma;
		p_tx_dma->dir = BTIF_TX;
		p_tx_dma->p_btif = &(g_btif[index]);

/*DMA Tx vFIFO initialization*/
		p_tx_dma->p_dma_info = hal_btif_dma_info_get(DMA_DIR_TX);
/*spinlock init*/
		spin_lock_init(&(p_tx_dma->iolock));
/*entry setup*/
		atomic_set(&(p_tx_dma->entry), 0);
/*vFIFO initialization*/
		i_ret = _btif_vfifo_init(p_tx_dma);
		if (i_ret != 0) {
			BTIF_ERR_FUNC("BTIF Tx vFIFO allocation failed\n");
			goto err_exit2;
		}

#if !(MTK_BTIF_ENABLE_CLK_REF_COUNTER)
/*enable BTIF Tx DMA channel's clock gating by default*/
		i_ret = hal_btif_dma_clk_ctrl(p_tx_dma->p_dma_info,
					      CLK_OUT_DISABLE);
		if (i_ret != 0) {
			BTIF_ERR_FUNC("BTIF Tx DMA's CG failed\n");
			goto err_exit2;
		}
#endif

#else
		g_btif[index].p_tx_dma = NULL;
/*force tx mode to DMA no matter what it is in default setting*/
		g_btif[index].tx_mode = BTIF_MODE_PIO;
#endif

#if ENABLE_BTIF_RX_DMA
		p_rx_dma = &g_dma[index][BTIF_RX];
		g_btif[index].p_rx_dma = p_rx_dma;
		p_rx_dma->p_btif = &(g_btif[index]);
		p_rx_dma->dir = BTIF_RX;

/*DMA Tx vFIFO initialization*/
		p_rx_dma->p_dma_info = hal_btif_dma_info_get(DMA_DIR_RX);
/*spinlock init*/
		spin_lock_init(&(p_rx_dma->iolock));
/*entry setup*/
		atomic_set(&(p_rx_dma->entry), 0);
/*vFIFO initialization*/
		i_ret = _btif_vfifo_init(p_rx_dma);
		if (i_ret != 0) {
			BTIF_ERR_FUNC("BTIF Rx vFIFO allocation failed\n");
			goto err_exit2;
		}

#if !(MTK_BTIF_ENABLE_CLK_REF_COUNTER)
/*enable BTIF Tx DMA channel's clock gating by default*/
		i_ret = hal_btif_dma_clk_ctrl(p_rx_dma->p_dma_info,
					      CLK_OUT_DISABLE);
		if (i_ret != 0) {
			BTIF_ERR_FUNC("BTIF Rx DMA's CG failed\n");
			goto err_exit2;
		}
#endif

#else
		g_btif[index].p_rx_dma = NULL;
/*force rx mode to DMA no matter what it is in default setting*/
		g_btif[index].rx_mode = BTIF_MODE_PIO;

#endif
/*PM state mechine initialization*/
		i_ret = _btif_state_init(&(g_btif[index]));
		if (i_ret != 0) {
			BTIF_ERR_FUNC("BTIF state mechanism init failed\n");
			goto err_exit2;
		}

/*Rx bottom half initialization*/
		i_ret = _btif_rx_btm_init(&(g_btif[index]));
		if (i_ret != 0) {
			BTIF_ERR_FUNC("BTIF Rx btm init failed\n");
			goto err_exit3;
		}
		i_ret = _btif_tx_ctx_init(&(g_btif[index]));
		if (i_ret != 0) {
			BTIF_ERR_FUNC("BTIF Tx context init failed\n");
			goto err_exit4;
		}
/*Character Device initialization*/
/*Chaozhong: ToDo: to be initialized*/

		mutex_init(&g_btif[index].ops_mtx);
	}

/*Debug purpose initialization*/

#if BTIF_CDEV_SUPPORT
	btif_chrdev_init();
#endif

	return 0;

err_exit4:
	for (index = 0; index < BTIF_PORT_NR; index++)
		_btif_tx_ctx_deinit(&(g_btif[index]));

err_exit3:
	for (index = 0; index < BTIF_PORT_NR; index++) {
		_btif_rx_btm_deinit(&(g_btif[index]));

		_btif_state_deinit(&(g_btif[index]));
	}

err_exit2:
	for (index = 0; index < BTIF_PORT_NR; index++) {
		p_tx_dma = &g_dma[index][BTIF_TX];
		p_rx_dma = &g_dma[index][BTIF_RX];
#if ENABLE_BTIF_TX_DMA
		_btif_vfifo_deinit(p_tx_dma);
#endif

#if ENABLE_BTIF_RX_DMA
		_btif_vfifo_deinit(p_rx_dma);
#endif
		g_btif[index].open_counter = 0;
		g_btif[index].enable = false;
	}
	driver_remove_file(&mtk_btif_dev_drv.driver, &driver_attr_flag);
	platform_driver_unregister(&mtk_btif_dev_drv);

err_exit1:
	i_ret = -1;
	BTIF_DBG_FUNC("--\n");
	return i_ret;
}

static void BTIF_exit(void)
{
	unsigned int index = 0;
	p_mtk_btif_dma p_tx_dma = NULL;
	p_mtk_btif_dma p_rx_dma = NULL;

	BTIF_DBG_FUNC("++\n");

	for (index = 0; index < BTIF_PORT_NR; index++) {
		g_btif[index].open_counter = 0;
		g_btif[index].enable = false;
		p_tx_dma = &g_dma[index][BTIF_TX];
		p_rx_dma = &g_dma[index][BTIF_RX];
#if ENABLE_BTIF_TX_DMA
		_btif_vfifo_deinit(p_tx_dma);
#endif

#if ENABLE_BTIF_RX_DMA
		_btif_vfifo_deinit(p_rx_dma);
#endif
		_btif_state_deinit(&(g_btif[index]));

		_btif_rx_btm_deinit(&(g_btif[index]));

		mutex_destroy(&g_btif[index].ops_mtx);
	}

#if !defined(CONFIG_MTK_CLKMGR)
		hal_btif_clk_unprepare();
#endif

	driver_remove_file(&mtk_btif_dev_drv.driver, &driver_attr_flag);
	platform_driver_unregister(&mtk_btif_dev_drv);
	BTIF_DBG_FUNC("--\n");
}

int mtk_btif_hal_get_log_lvl(void)
{
	return mtk_btif_dbg_lvl;
}

void mtk_btif_read_cpu_sw_rst_debug(void)
{
	mtk_btif_read_cpu_sw_rst_debug_plat();
}

/*---------------------------------------------------------------------------*/

module_init(BTIF_init);
module_exit(BTIF_exit);

/*---------------------------------------------------------------------------*/

MODULE_AUTHOR("MBJ/WCN/SE/SS1/Chaozhong.Liang");
MODULE_DESCRIPTION("MTK BTIF Driver$1.0$");
MODULE_LICENSE("GPL");

/*---------------------------------------------------------------------------*/

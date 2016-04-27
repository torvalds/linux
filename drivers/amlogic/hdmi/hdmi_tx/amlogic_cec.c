/* linux/drivers/amlogic/hdmi/hdmi_tx/amlogic_cec.c
 *
 * Copyright (c) 2016 Gerald Dachs
 *
 * CEC interface file for Amlogic
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/clk.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/semaphore.h>

#include <asm/uaccess.h>
#include <asm/delay.h>
#include <mach/am_regs.h>
#include <mach/power_gate.h>
#include <linux/amlogic/tvin/tvin.h>

#include <mach/gpio.h>
#include <linux/amlogic/hdmi_tx/hdmi_info_global.h>
#include <linux/amlogic/hdmi_tx/hdmi_tx_module.h>
#include <mach/hdmi_tx_reg.h>
#include <linux/amlogic/hdmi_tx/hdmi_tx_cec.h>

#define CONFIG_TV_DEBUG // for verbose output
//#undef CONFIG_TV_DEBUG
unsigned long amlogic_cec_debug_flag = 1;

MODULE_AUTHOR("Gerald Dachs");
MODULE_DESCRIPTION("Amlogic CEC driver");
MODULE_LICENSE("GPL");

//unused, only left to satisfy the linker
bool cec_msg_dbg_en = 1;

#define DRV_NAME "amlogic_cec"
#ifndef amlogic_cec_log_dbg
#define amlogic_cec_log_dbg(fmt, ...) \
    if (amlogic_cec_debug_flag)       \
	printk(KERN_INFO "[%s] %s(): " fmt, DRV_NAME, __func__, ##__VA_ARGS__)
#endif

#define CEC_IOC_MAGIC        'c'
#define CEC_IOC_SETLADDR     _IOW(CEC_IOC_MAGIC, 0, unsigned int)
#define CEC_IOC_GETPADDR     _IO(CEC_IOC_MAGIC, 1)

#define VERSION   "0.0.1" /* Driver version number */
#define CEC_MINOR 243	/* Major 10, Minor 242, /dev/cec */

/* CEC Rx buffer size */
#define CEC_RX_BUFF_SIZE            16
/* CEC Tx buffer size */
#define CEC_TX_BUFF_SIZE            16

static DEFINE_SEMAPHORE(init_mutex);

struct cec_rx_list {
	u8 buffer[CEC_RX_BUFF_SIZE];
	unsigned char size;
	struct list_head list;
};

struct cec_rx_struct {
	spinlock_t lock;
	wait_queue_head_t waitq;
	atomic_t state;
	struct list_head list;
};

struct cec_tx_struct {
	spinlock_t lock;
	wait_queue_head_t waitq;
	atomic_t state;
};

enum cec_state {
	STATE_RX,
	STATE_TX,
	STATE_DONE,
	STATE_ERROR
};

static char banner[] __initdata =
    "Amlogic CEC Driver, (c) 2016 Gerald Dachs";

static struct cec_rx_struct cec_rx_struct;

static struct cec_tx_struct cec_tx_struct;

static atomic_t hdmi_on = ATOMIC_INIT(0);

cec_global_info_t cec_global_info;

static hdmitx_dev_t* hdmitx_device = NULL;

static void amlogic_cec_set_rx_state(enum cec_state state)
{
    atomic_set(&cec_rx_struct.state, state);
}

static void amlogic_cec_set_tx_state(enum cec_state state)
{
    atomic_set(&cec_tx_struct.state, state);
}

static void amlogic_cec_msg_dump(char * msg_tag, const unsigned char *data, unsigned char count)
{
  int i;
  int pos;
  unsigned char msg_log_buf[128] = { 0 };

  if (amlogic_cec_debug_flag == 1)
  {
      pos = 0;
      pos += sprintf(msg_log_buf + pos, "msg %s len: %d   dat: ", msg_tag, count);
      for (i = 0; i < count; ++i)
      {
          pos += sprintf(msg_log_buf + pos, "%02x ", data[i]);
      }
      pos += sprintf(msg_log_buf + pos, "\n");
      msg_log_buf[pos] = '\0';
      hdmi_print(INF, "[amlogic_cec] dump: %s", msg_log_buf);
  }
}

static unsigned int amlogic_cec_read_reg(unsigned int reg)
{
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6
    return hdmi_rd_reg(CEC0_BASE_ADDR + reg);
#endif
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    return aocec_rd_reg(reg);
#endif
}

static void amlogic_cec_write_reg(unsigned int reg, unsigned int value)
{
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6
    hdmi_wr_reg(CEC0_BASE_ADDR + reg, value);
#endif
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    aocec_wr_reg(reg, value);
#endif
}

static int amlogic_cec_read_hw(unsigned char *data, unsigned char *count)
{
    int ret = -1;
    int valid_msg;
    int rx_msg_status;
    int rx_num_msg;

    rx_msg_status = amlogic_cec_read_reg(CEC_RX_MSG_STATUS);
    rx_num_msg = amlogic_cec_read_reg(CEC_RX_NUM_MSG);

    amlogic_cec_log_dbg("rx_msg_status: %d, rx_num_msg: %d\n", rx_msg_status, rx_num_msg);

    valid_msg = (RX_DONE == rx_msg_status) && (1 == rx_num_msg);

    if (valid_msg)
    {
      int i;

      *count = amlogic_cec_read_reg(CEC_RX_MSG_LENGTH) + 1;
      for (i = 0; i < (*count) && i < CEC_RX_BUFF_SIZE; ++i)
      {
          data[i]= amlogic_cec_read_reg(CEC_RX_MSG_0_HEADER + i);
      }

      amlogic_cec_msg_dump("RX", data, *count);

      ret = RX_DONE;
    }

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    aml_write_reg32(P_AO_CEC_INTR_CLR, aml_read_reg32(P_AO_CEC_INTR_CLR) | (1 << 2));
#endif
    amlogic_cec_write_reg(CEC_RX_MSG_CMD, valid_msg ? RX_ACK_NEXT : RX_ACK_CURRENT);
    amlogic_cec_write_reg(CEC_RX_MSG_CMD, RX_NO_OP);

    return ret;
}


static void amlogic_cec_write_hw(const char *data, size_t count)
{
  int i;

  for (i = 0; i < count; ++i)
  {
      amlogic_cec_write_reg(CEC_TX_MSG_0_HEADER + i, data[i]);
  }
  amlogic_cec_write_reg(CEC_TX_MSG_LENGTH, count - 1);
  amlogic_cec_write_reg(CEC_TX_MSG_CMD, TX_REQ_CURRENT);

  amlogic_cec_msg_dump("TX", data, count);
}

unsigned short cec_log_addr_to_dev_type(unsigned char log_addr)
{
// unused, just to satisfy the linker
  return log_addr;
}

void cec_node_init(hdmitx_dev_t* hdmitx_device)
{
    unsigned long cec_phy_addr;
    unsigned long spin_flags;
    struct cec_rx_list *entry;

    cec_phy_addr = (((hdmitx_device->hdmi_info.vsdb_phy_addr.a) & 0xf) << 12)
                 | (((hdmitx_device->hdmi_info.vsdb_phy_addr.b) & 0xf) << 8)
                 | (((hdmitx_device->hdmi_info.vsdb_phy_addr.c) & 0xf) << 4)
                 | (((hdmitx_device->hdmi_info.vsdb_phy_addr.d) & 0xf) << 0);

    // If VSDB is not valid,use last or default physical address.
    if (hdmitx_device->hdmi_info.vsdb_phy_addr.valid == 0)
    {
	amlogic_cec_log_dbg("no valid cec physical address\n");
	if (aml_read_reg32(P_AO_DEBUG_REG1) & 0xffff)
	{
	    amlogic_cec_log_dbg("use last physical address\n");
	}
	else
	{
	    aml_write_reg32(P_AO_DEBUG_REG1, (aml_read_reg32(P_AO_DEBUG_REG1) & (0xf << 16)) | 0x1000);
	    amlogic_cec_log_dbg("use default physical address\n");
	}
    }
    else
    {
	if (cec_global_info.my_node_index)
	{
	    // prevent write operations
	    if (down_interruptible(&init_mutex))
	    {
		printk(KERN_ERR "[amlogic] ##### cec node init interrupted! #####\n");
		return;
	    }
	    hdmitx_device->cec_init_ready = 0;
            spin_lock_irqsave(&cec_rx_struct.lock, spin_flags);

	    amlogic_cec_log_dbg("start reset\n");
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6
	    aml_write_reg32(P_HDMI_CTRL_PORT, aml_read_reg32(P_HDMI_CTRL_PORT)|(1<<16));
	    hdmi_wr_reg(OTHER_BASE_ADDR+HDMI_OTHER_CTRL0, 0xc); //[3]cec_creg_sw_rst [2]cec_sys_sw_rst

#if 0
	    hdmi_wr_reg(CEC0_BASE_ADDR+CEC_TX_CLEAR_BUF, 0x1);
	    hdmi_wr_reg(CEC0_BASE_ADDR+CEC_RX_CLEAR_BUF, 0x1);

	    hdmi_wr_reg(CEC0_BASE_ADDR+CEC_TX_CLEAR_BUF, 0x0);
	    hdmi_wr_reg(CEC0_BASE_ADDR+CEC_RX_CLEAR_BUF, 0x0);
#endif

	    hdmi_wr_reg(OTHER_BASE_ADDR+HDMI_OTHER_CTRL0, 0x0);
	    aml_write_reg32(P_HDMI_CTRL_PORT, aml_read_reg32(P_HDMI_CTRL_PORT)&(~(1<<16)));
	    hdmi_wr_reg(CEC0_BASE_ADDR+CEC_CLOCK_DIV_H, 0x00 );
	    hdmi_wr_reg(CEC0_BASE_ADDR+CEC_CLOCK_DIV_L, 0xf0 );

#endif
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	    // regain rx interrupts
	    cec_enable_irq();
#endif
            spin_unlock_irqrestore(&cec_rx_struct.lock, spin_flags);

            hdmitx_device->cec_init_ready = 1;

	    up(&init_mutex);
	    amlogic_cec_log_dbg("stop reset\n");
	}

	if ((aml_read_reg32(P_AO_DEBUG_REG1) & 0xffff) != cec_phy_addr)
	{
	    aml_write_reg32(P_AO_DEBUG_REG1, (aml_read_reg32(P_AO_DEBUG_REG1) & (0xf << 16)) | cec_phy_addr);
	    amlogic_cec_log_dbg("physical address:0x%x\n", aml_read_reg32(P_AO_DEBUG_REG1) & 0xffff);

	    if ((hdmitx_device->cec_init_ready != 0) && (hdmitx_device->hpd_state != 0))
	    {
		if ((entry = kmalloc(sizeof(struct cec_rx_list), GFP_ATOMIC)) == NULL)
		{
		    amlogic_cec_log_dbg("can't alloc cec_rx_list\n");
		}
		else
		{
		    // let the libCEC ask for new physical Address
		    entry->buffer[0] = 0xff;
		    entry->size = 1;
		    INIT_LIST_HEAD(&entry->list);

		    spin_lock_irqsave(&cec_rx_struct.lock, spin_flags);
		    list_add_tail(&entry->list, &cec_rx_struct.list);
		    amlogic_cec_set_rx_state(STATE_DONE);
		    spin_unlock_irqrestore(&cec_rx_struct.lock, spin_flags);

		    amlogic_cec_log_dbg("trigger libCEC\n");
		    wake_up_interruptible(&cec_rx_struct.waitq);
		}
	    }
	}
    }
}

static irqreturn_t amlogic_cec_irq_handler(int irq, void *dummy)
{
    unsigned long spin_flags;
    struct cec_rx_list *entry;
    unsigned int tx_msg_state;
    unsigned int rx_msg_state;

#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6
    udelay(100); //Delay execution a little. This fixes an issue when HDMI CEC stops working after a while.
#endif

    tx_msg_state = amlogic_cec_read_reg(CEC_TX_MSG_STATUS);
    rx_msg_state = amlogic_cec_read_reg(CEC_RX_MSG_STATUS);

    amlogic_cec_log_dbg("cec msg status: rx: 0x%x; tx: 0x%x\n", rx_msg_state, tx_msg_state);

    if ((tx_msg_state == TX_DONE) || (tx_msg_state == TX_ERROR))
    {
	amlogic_cec_write_reg(CEC_TX_MSG_CMD, TX_NO_OP);

	switch (tx_msg_state) {
	  case TX_ERROR :
	    amlogic_cec_set_tx_state(STATE_ERROR);
	    break;
	  case TX_DONE :
	    amlogic_cec_set_tx_state(STATE_DONE);
	    break;
	}
	wake_up_interruptible(&cec_tx_struct.waitq);
    }

    if (rx_msg_state == RX_DONE)
    {

	if ((entry = kmalloc(sizeof(struct cec_rx_list), GFP_ATOMIC)) == NULL)
	{
	    amlogic_cec_log_dbg("can't alloc cec_rx_list\n");
	    return IRQ_HANDLED;
	}

	INIT_LIST_HEAD(&entry->list);

	spin_lock_irqsave(&cec_rx_struct.lock, spin_flags);

	if ((-1) == amlogic_cec_read_hw(entry->buffer, &entry->size))
	{
	    kfree(entry);
	    amlogic_cec_log_dbg("amlogic_cec_irq_handler: nothing to read\n");
            spin_unlock_irqrestore(&cec_rx_struct.lock, spin_flags);
	    return IRQ_HANDLED;
	}

	list_add_tail(&entry->list, &cec_rx_struct.list);
	amlogic_cec_set_rx_state(STATE_DONE);
	spin_unlock_irqrestore(&cec_rx_struct.lock, spin_flags);

	wake_up_interruptible(&cec_rx_struct.waitq);
    }

    return IRQ_HANDLED;
}

static int amlogic_cec_open(struct inode *inode, struct file *file)
{
    int ret = 0;

    if (atomic_read(&hdmi_on))
    {
	amlogic_cec_log_dbg("do not allow multiple open for tvout cec\n");
	ret = -EBUSY;
    }
    else
    {
	atomic_inc(&hdmi_on);
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6
        if (request_irq(INT_HDMI_CEC, &amlogic_cec_irq_handler,
        IRQF_SHARED, "amhdmitx-cec",(void *)hdmitx_device))
        {
    	    amlogic_cec_log_dbg("Can't register IRQ %d\n",INT_HDMI_CEC);
            return -EFAULT;
        }
#endif
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
        if (request_irq(INT_AO_CEC, &amlogic_cec_irq_handler,
        IRQF_SHARED, "amhdmitx-aocec",(void *)hdmitx_device))
        {
    	    amlogic_cec_log_dbg("Can't register IRQ %d\n",INT_HDMI_CEC);
            return -EFAULT;
        }
#endif

#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6
        cec_gpi_init();
#endif

#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6
        aml_set_reg32_bits(P_PERIPHS_PIN_MUX_1, 1, 25, 1);
        // Clear CEC Int. state and set CEC Int. mask
        aml_write_reg32(P_SYS_CPU_0_IRQ_IN1_INTR_STAT_CLR, aml_read_reg32(P_SYS_CPU_0_IRQ_IN1_INTR_STAT_CLR) | (1 << 23));    // Clear the interrupt
        aml_write_reg32(P_SYS_CPU_0_IRQ_IN1_INTR_MASK, aml_read_reg32(P_SYS_CPU_0_IRQ_IN1_INTR_MASK) | (1 << 23));            // Enable the hdmi cec interrupt
#endif
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
// GPIOAO_12
        aml_set_reg32_bits(P_AO_RTI_PIN_MUX_REG, 0, 14, 1);       // bit[14]: AO_PWM_C pinmux                  //0xc8100014
        aml_set_reg32_bits(P_AO_RTI_PULL_UP_REG, 1, 12, 1);       // bit[12]: enable AO_12 internal pull-up   //0xc810002c
        aml_set_reg32_bits(P_AO_RTI_PIN_MUX_REG, 1, 17, 1);       // bit[17]: AO_CEC pinmux                    //0xc8100014
        ao_cec_init();
        cec_arbit_bit_time_set(3, 0x118, 0);
        cec_arbit_bit_time_set(5, 0x000, 0);
        cec_arbit_bit_time_set(7, 0x2aa, 0);
#endif
        amlogic_cec_write_reg(CEC_LOGICAL_ADDR0, (0x1 << 4) | 0xf);

        hdmitx_device->cec_init_ready = 1;

    }
    return ret;
}

static int amlogic_cec_release(struct inode *inode, struct file *file)
{
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6
    aml_write_reg32(P_SYS_CPU_0_IRQ_IN1_INTR_MASK, aml_read_reg32(P_SYS_CPU_0_IRQ_IN1_INTR_MASK) & ~(1 << 23));            // Disable the hdmi cec interrupt
    free_irq(INT_HDMI_CEC, (void *)hdmitx_device);
#endif
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    free_irq(INT_AO_CEC, (void *)hdmitx_device);
#endif

    amlogic_cec_write_reg(CEC_LOGICAL_ADDR0, (0x1 << 4) | 0xf);

    atomic_dec(&hdmi_on);

    return 0;
}

static ssize_t amlogic_cec_read(struct file *file, char __user *buffer,
				size_t count, loff_t *ppos)
{
    ssize_t retval;
    unsigned long spin_flags;
    struct cec_rx_list* entry = NULL;

    if (wait_event_interruptible(cec_rx_struct.waitq,
				 atomic_read(&cec_rx_struct.state) == STATE_DONE))
    {
	amlogic_cec_log_dbg("error during wait on state change\n");
	return -ERESTARTSYS;
    }

    spin_lock_irqsave(&cec_rx_struct.lock, spin_flags);

    entry = list_first_entry_or_null(&cec_rx_struct.list, struct cec_rx_list, list);

    if (entry == NULL || entry->size > count)
    {
	amlogic_cec_log_dbg("entry is NULL, or empty\n");
	retval = -1;
	goto error_exit;
    }

    if (copy_to_user(buffer, entry->buffer, entry->size))
    {
	printk(KERN_ERR " copy_to_user() failed!\n");

	retval = -EFAULT;
	goto error_exit;
    }

    retval = entry->size;

error_exit:
    if (entry != NULL)
    {
    	list_del(&entry->list);
    	kfree(entry);
    }

    if (list_empty(&cec_rx_struct.list))
    {
        amlogic_cec_set_rx_state(STATE_RX);
    }

    spin_unlock_irqrestore(&cec_rx_struct.lock, spin_flags);

    return retval;
}

static ssize_t amlogic_cec_write(struct file *file, const char __user *buffer,
			size_t count, loff_t *ppos)
{
    int retval = count;
    char data[CEC_TX_BUFF_SIZE];

    /* check data size */
    if (count > CEC_TX_BUFF_SIZE || count == 0)
	return -1;

    if (copy_from_user(data, buffer, count))
    {
	printk(KERN_ERR " copy_from_user() failed!\n");
	return -EFAULT;
    }

    amlogic_cec_set_tx_state(STATE_TX);

    // don't write if cec_node_init() is in progress
    if (down_interruptible(&init_mutex))
    {
	amlogic_cec_log_dbg("error during wait on state change\n");
	printk(KERN_ERR "[amlogic] ##### cec write error! #####\n");
	return -ERESTARTSYS;
    }

    amlogic_cec_write_hw(data, count);

    if (wait_event_interruptible_timeout(cec_tx_struct.waitq,
        atomic_read(&cec_tx_struct.state) != STATE_TX, 2 * HZ) <= 0)
    {
	amlogic_cec_log_dbg("error during wait on state change, resetting\n");
	printk(KERN_ERR "[amlogic] ##### cec write error! #####\n");
	amlogic_cec_write_reg(CEC_TX_MSG_CMD, TX_ABORT); // stop cec tx for hw retry.
	amlogic_cec_write_reg(CEC_TX_MSG_CMD, TX_NO_OP);
	retval = -ERESTARTSYS;
	goto error_exit;
    }

    if (atomic_read(&cec_tx_struct.state) != STATE_DONE)
    {
	printk(KERN_ERR "[amlogic] ##### cec write error! #####\n");
	retval = -1;
    }

error_exit:
    up(&init_mutex);

    return retval;
}

static long amlogic_cec_ioctl(struct file *file, unsigned int cmd,
						unsigned long arg)
{
    unsigned char logical_addr;
    unsigned int reg;

    switch(cmd) {
    case CEC_IOC_SETLADDR:
        if (get_user(logical_addr, (unsigned char __user *)arg))
        {
            amlogic_cec_log_dbg("Failed to get logical addr from user\n");
            return -EFAULT;
        }

        amlogic_cec_write_reg(CEC_LOGICAL_ADDR0, (0x1 << 4) | logical_addr);
        cec_global_info.my_node_index = logical_addr;
        /*
         * use DEBUG_REG1 bit 16 ~ 31 to save logic address.
         * So uboot can use this logic address directly
         */
        reg  = (aml_read_reg32(P_AO_DEBUG_REG1) & 0xffff);
        reg |= ((unsigned int)logical_addr) << 16;
        aml_write_reg32(P_AO_DEBUG_REG1, reg);

        amlogic_cec_log_dbg("amlogic_cec_ioctl: Set logical address: %d\n", logical_addr);
        return 0;

    case CEC_IOC_GETPADDR:
        amlogic_cec_log_dbg("amlogic_cec_ioctl: return physical address 0x%x\n", aml_read_reg32(P_AO_DEBUG_REG1) & 0xffff);
    	return aml_read_reg32(P_AO_DEBUG_REG1) & 0xffff;
    }

    return -EINVAL;
}

static u32 amlogic_cec_poll(struct file *file, poll_table *wait)
{
    if (atomic_read(&cec_rx_struct.state) != STATE_DONE)
    {
        poll_wait(file, &cec_rx_struct.waitq, wait);
    }
    if (atomic_read(&cec_rx_struct.state) == STATE_DONE)
    {
	return POLLIN | POLLRDNORM;
    }
    return 0;
}

static const struct file_operations cec_fops = {
    .owner   = THIS_MODULE,
    .open    = amlogic_cec_open,
    .release = amlogic_cec_release,
    .read    = amlogic_cec_read,
    .write   = amlogic_cec_write,
    .unlocked_ioctl = amlogic_cec_ioctl,
    .poll    = amlogic_cec_poll,
};

static struct miscdevice cec_misc_device = {
    .minor = CEC_MINOR,
    .name  = "AmlogicCEC",
    .fops  = &cec_fops,
};

static int amlogic_cec_init(void)
{
    int retval = 0;
    extern hdmitx_dev_t * get_hdmitx_device(void);

    if (down_interruptible(&init_mutex))
    {
      return -ERESTARTSYS;
    }

    INIT_LIST_HEAD(&cec_rx_struct.list);

    printk("%s, Version: %s\n", banner, VERSION);

    hdmitx_device = get_hdmitx_device();
    amlogic_cec_log_dbg("CEC init\n");

    amlogic_cec_write_reg(CEC_LOGICAL_ADDR0, (0x1 << 4) | 0xf);

#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6
    hdmi_wr_reg(CEC0_BASE_ADDR+CEC_CLOCK_DIV_H, 0x00 );
    hdmi_wr_reg(CEC0_BASE_ADDR+CEC_CLOCK_DIV_L, 0xf0 );
#endif

    init_waitqueue_head(&cec_rx_struct.waitq);

    spin_lock_init(&cec_rx_struct.lock);

    init_waitqueue_head(&cec_tx_struct.waitq);

    spin_lock_init(&cec_tx_struct.lock);

    if (misc_register(&cec_misc_device))
    {
	printk(KERN_WARNING " Couldn't register device 10, %d.\n", CEC_MINOR);
	retval = -EBUSY;
    }

    // release initial lock on init_mutex
    up(&init_mutex);

    amlogic_cec_log_dbg("CEC init finished: %d\n", retval);

    return retval;
}

static void amlogic_cec_exit(void)
{
    misc_deregister(&cec_misc_device);
}

module_init(amlogic_cec_init);
module_exit(amlogic_cec_exit);

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/serial_reg.h>
#include <linux/circ_buf.h>
#include <linux/gfp.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <mach/rk2818_iomap.h>

#include <mach/spi_fpga.h>

#if defined(CONFIG_SPI_DPRAM_DEBUG)
#define DBG(x...)   printk(x)
#else
#define DBG(x...)
#endif

#define SPI_DPRAM_TEST 0
/*****RAM0 for bp write and ap read*****/
#define SPI_DPRAM_BPWRITE_START 0
#define SPI_DPRAM_BPWRITE_END	0x0fff
#define SPI_DPRAM_BPWRITE_SIZE	0x1000	// 4K*16bits
/*****RAM1 for ap write and bp read*****/
#define SPI_DPRAM_APWRITE_START 0x1000
#define SPI_DPRAM_APWRITE_END   0x17ff
#define SPI_DPRAM_APWRITE_SIZE	0x0800	// 2K*16bits
/*****RAM2 for log of bp write and ap read*****/
#define SPI_DPRAM_LOG_BPWRITE_START 0x2000
#define SPI_DPRAM_LOG_BPWRITE_END	0x23ff
#define SPI_DPRAM_LOG_BPWRITE_SIZE	0x0400	// 1K*16bits
/*****RAM3 for log of ap write and bp read*****/
#define SPI_DPRAM_LOG_APWRITE_START 0x3000
#define SPI_DPRAM_LOG_APWRITE_END   0x33ff
#define SPI_DPRAM_LOG_APWRITE_SIZE	0x0400	// 1K*16bits

/*
#define BP_SEND_IN_PTR   0x3FEE
#define BP_SEND_OUT_PTR  0x3FF0

#define BP_READ_IN_PTR    0x3FF2
#define BP_READ_OUT_PTR   0x3FF4

#define BP_SEND_IN_PTR   0x3FF6
#define BP_SEND_OUT_PTR  0x3FF8

#define BP_READ_IN_PTR    0x3FFA
#define BP_READ_OUT_PTR   0x3FFC

#define BP_SEND_AP_Mailbox£º0x3ffe
#define AP_SEND_BP_Mailbox£º0x3fff

*/

#define SPI_DPRAM_PTR0_BPWRITE_APREAD	0X3fee
#define	SPI_DPRAM_PTR0_APWRITE_BPREAD	0X3ff0

#define SPI_DPRAM_PTR1_BPWRITE_APREAD	0x3ff2
#define	SPI_DPRAM_PTR1_APWRITE_BPREAD	0x3ff4

#define SPI_DPRAM_PTR2_BPWRITE_APREAD	0x3ff6
#define	SPI_DPRAM_PTR2_APWRITE_BPREAD	0x3ff8

#define SPI_DPRAM_PTR3_BPWRITE_APREAD	0x3ffa
#define	SPI_DPRAM_PTR3_APWRITE_BPREAD	0x3ffc

#define SPI_DPRAM_MAILBOX_BPWRITE	0x3ffe
#define SPI_DPRAM_MAILBOX_APWRITE	0x3fff

/*mailbox comminication's definition*/
#define MAILBOX_BPWRITE_DATA	0x01
#define MAILBOX_BPREAD_DATA		0x02
#define MAILBOX_APSEND_IRQ		0x03
#define MAILBOX_APSEND_ACK		0x04

#define TRUE 		1
#define FALSE 		0

static int spi_dpram_write_buf(struct spi_dpram *dpram, unsigned short int addr, unsigned char *buf, unsigned int len)
{	
	struct spi_fpga_port *port = container_of(dpram, struct spi_fpga_port, dpram);
	unsigned char opt = ((ICE_SEL_DPRAM & ICE_SEL_DPRAM_NOMAL) | ICE_SEL_DPRAM_WRITE);
	unsigned char *tx_buf = port->dpram.ptx;	
	int ret;
	*(port->dpram.ptx) = opt;
	*(port->dpram.ptx+1) = ((addr << 1) >> 8) & 0xff;
	*(port->dpram.ptx+2) = ((addr << 1) & 0xff);
	memcpy((port->dpram.ptx + 3), buf, len);
	
	DBG("%s:tx_buf=0x%x,port->dpram.ptx=0x%x,opt=0x%x,addr=0x%x,len=%d\n",__FUNCTION__,(int)tx_buf, (int)port->dpram.ptx, opt, addr&0xffff, len);
	ret = spi_write(port->spi, tx_buf, len+3);
	if(ret)
	printk("spi_write err!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\n\n");
	return 0;
}

static int spi_dpram_read_buf(struct spi_dpram *dpram, unsigned short int addr, unsigned char *buf, unsigned int len)
{
	struct spi_fpga_port *port = container_of(dpram, struct spi_fpga_port, dpram);
	unsigned char opt = ((ICE_SEL_DPRAM & ICE_SEL_DPRAM_NOMAL & ICE_SEL_DPRAM_READ));
	unsigned char tx_buf[4];
	unsigned char stat;
	
	tx_buf[0] = opt;
	tx_buf[1] = ((addr << 1) >> 8) & 0xff;
	tx_buf[2] = ((addr << 1) & 0xff);
	tx_buf[3] = 0;//give fpga 8 clks for reading data
	
	stat = spi_write_then_read(port->spi, tx_buf, sizeof(tx_buf), buf, len);	
	if(stat)
	{
		printk("%s:spi_write_then_read is error!,err=%d\n\n",__FUNCTION__,stat);
		return -1;
	}
	DBG("%s:opt=0x%x,addr=0x%x,len=%d\n",__FUNCTION__, opt, addr&0xffff, len);
	return 0;

}


int spi_dpram_write_ptr(struct spi_dpram *dpram, unsigned short int addr, unsigned int size)
{
	int ret;
	//int i;
	struct spi_fpga_port *port = container_of(dpram, struct spi_fpga_port, dpram);
	unsigned char opt = ((ICE_SEL_DPRAM & ICE_SEL_DPRAM_NOMAL) | ICE_SEL_DPRAM_WRITE);
	unsigned char tx_buf[5];
	
	tx_buf[0] = opt;
	tx_buf[1] = ((addr << 1) >> 8) & 0xff;
	tx_buf[2] = ((addr << 1) & 0xff);
	tx_buf[3] = (size>>8);
	tx_buf[4] = (size&0xff);

	//for(i=0;i<5;i++)
	//{
	//	printk("%s:tx_buf[%d]=0x%x\n",__FUNCTION__,i,tx_buf[i]);
	//}

	ret = spi_write(port->spi, tx_buf, sizeof(tx_buf));
	if(ret)
	{
		printk("%s:spi_write err!\n",__FUNCTION__);
		return -1;
	}
	
	return 0;

}


int spi_dpram_read_ptr(struct spi_dpram *dpram, unsigned short int addr)
{
	int ret;
	struct spi_fpga_port *port = container_of(dpram, struct spi_fpga_port, dpram);
	unsigned char opt = ((ICE_SEL_DPRAM & ICE_SEL_DPRAM_NOMAL & ICE_SEL_DPRAM_READ));
	unsigned char tx_buf[4],rx_buf[2];
	
	tx_buf[0] = opt;
	tx_buf[1] = ((addr << 1) >> 8) & 0xff;
	tx_buf[2] = ((addr << 1) & 0xff);
	tx_buf[3] = 0;//give fpga 8 clks for reading data

	ret = spi_write_then_read(port->spi, tx_buf, sizeof(tx_buf), rx_buf, sizeof(rx_buf));
	if(ret)
	{
		printk("%s:spi_write_then_read err!\n",__FUNCTION__);
		return -1;
	}
	
	ret = (rx_buf[0] << 8) | rx_buf[1];
	
	return (ret&0xffff);

}


int spi_dpram_write_mailbox(struct spi_dpram *dpram, unsigned int mailbox)
{
	int ret;
	struct spi_fpga_port *port = container_of(dpram, struct spi_fpga_port, dpram);
	unsigned char opt = ((ICE_SEL_DPRAM & ICE_SEL_DPRAM_NOMAL) | ICE_SEL_DPRAM_WRITE);
	unsigned char tx_buf[5];
	
	tx_buf[0] = opt;
	tx_buf[1] = ((SPI_DPRAM_MAILBOX_APWRITE << 1) >> 8) & 0xff;
	tx_buf[2] = ((SPI_DPRAM_MAILBOX_APWRITE << 1) & 0xff);
	tx_buf[3] = mailbox>>8;
	tx_buf[4] = mailbox&0xff;
	
	ret = spi_write(port->spi, tx_buf, sizeof(tx_buf));
	if(ret)
	{
		printk("%s:spi_write err!\n",__FUNCTION__);
		return -1;
	}
	
	return 0;
}


int spi_dpram_read_mailbox(struct spi_dpram *dpram)
{
	int ret;
	struct spi_fpga_port *port = container_of(dpram, struct spi_fpga_port, dpram);
	unsigned char opt = ((ICE_SEL_DPRAM & ICE_SEL_DPRAM_NOMAL & ICE_SEL_DPRAM_READ));
	unsigned char tx_buf[4],rx_buf[2];
	
	tx_buf[0] = opt;
	tx_buf[1] = ((SPI_DPRAM_MAILBOX_BPWRITE << 1) >> 8) & 0xff;
	tx_buf[2] = ((SPI_DPRAM_MAILBOX_BPWRITE << 1) & 0xff);
	tx_buf[3] = 0;//give fpga 8 clks for reading data

	ret = spi_write_then_read(port->spi, tx_buf, sizeof(tx_buf), rx_buf, sizeof(rx_buf));
	if(ret)
	{
		printk("%s:spi_write_then_read err!\n",__FUNCTION__);
		return -1;
	}
	
	return ((rx_buf[0]<<8) | rx_buf[1]);
}


static void spi_dpram_handle_busy(struct spi_device *spi)
{	

}


static void spi_dpram_busy_work_handler(struct work_struct *work)
{
	struct spi_fpga_port *port =
		container_of(work, struct spi_fpga_port, dpram.spi_dpram_busy_work);
	spi_dpram_handle_busy(port->spi);
}


static irqreturn_t spi_dpram_busy_irq(int irq, void *dev_id)
{
	struct spi_fpga_port *port = dev_id;

	DBG("Enter::%s,LINE=%d\n",__FUNCTION__,__LINE__);
	/*
	 * Can't do anything in interrupt context because we need to
	 * block (spi_sync() is blocking) so fire of the interrupt
	 * handling workqueue.
	 * Remember that we access ICE65LXX registers through SPI bus
	 * via spi_sync() call.
	 */

	queue_work(port->dpram.spi_dpram_busy_workqueue, &port->dpram.spi_dpram_busy_work);
	
	return IRQ_HANDLED;
}

#if SPI_DPRAM_TEST
#define SEL_RAM0	0
#define SEL_RAM1	1
#define SEL_RAM2	2
#define SEL_RAM3	3
#define SEL_REG		4
#define SEL_RAM		SEL_RAM2
#define DPRAM_TEST_LEN 16	//8bit
unsigned char buf_test_dpram[DPRAM_TEST_LEN];
void spi_dpram_work_handler(struct work_struct *work)
{
	int i,j;
	int ret;
	struct spi_fpga_port *port =
		container_of(work, struct spi_fpga_port, dpram.spi_dpram_work);
	printk("*************test spi_dpram now***************\n");
	
#if(SEL_RAM == SEL_RAM0)
	//RAM0
	for(i=0;i<(SPI_DPRAM_BPWRITE_SIZE/(DPRAM_TEST_LEN>>1));i++)
	{
		port->dpram.read_dpram(&port->dpram, SPI_DPRAM_BPWRITE_START+(i*DPRAM_TEST_LEN>>1), port->dpram.prx+i*DPRAM_TEST_LEN, DPRAM_TEST_LEN);
	}
	
	for(i=0;i<SPI_DPRAM_BPWRITE_SIZE;i++)
	{
		ret = (*(port->dpram.prx+2*i)<<8) | (*(port->dpram.prx+2*i+1));
		if(ret != 0xa000+i)
		printk("prx[%d]=0x%x ram[%d]=0x%x\n",i,ret&0xffff,i,0xa000+i);
	}
	
#elif(SEL_RAM == SEL_RAM1)	
	//RAM1
	for(i=0;i<(SPI_DPRAM_APWRITE_SIZE/(DPRAM_TEST_LEN>>1));i++)
	{				
		for(j=(i*(DPRAM_TEST_LEN>>1)); j<((i+1)*(DPRAM_TEST_LEN>>1)); j++)
		{
			buf_test_dpram[2*(j-(i*(DPRAM_TEST_LEN>>1)))] = (0xa000+j)>>8;
			buf_test_dpram[2*(j-(i*(DPRAM_TEST_LEN>>1)))+1] = (0xa000+j)&0xff;
			printk("buf_test_dpram[%d]=0x%x\n",j,buf_test_dpram[(j-(i*(DPRAM_TEST_LEN>>1)))]);
		}
		
		port->dpram.write_dpram(&port->dpram, ((DPRAM_TEST_LEN*i)>>1)+SPI_DPRAM_APWRITE_START, buf_test_dpram, sizeof(buf_test_dpram));
		mdelay(1);
	}
	
#elif(SEL_RAM == SEL_RAM2)
	//RAM2
	for(i=0;i<(SPI_DPRAM_LOG_BPWRITE_SIZE/(DPRAM_TEST_LEN>>1));i++)
	{
		port->dpram.read_dpram(&port->dpram, SPI_DPRAM_LOG_BPWRITE_START+(i*DPRAM_TEST_LEN>>1), port->dpram.prx+i*DPRAM_TEST_LEN, DPRAM_TEST_LEN);
	}
	
	for(i=0;i<SPI_DPRAM_LOG_BPWRITE_SIZE;i++)
	{
		ret = (*(port->dpram.prx+2*i)<<8) | (*(port->dpram.prx+2*i+1));
		if(ret != 0xc000+i)
		printk("prx[%d]=0x%x ram[%d]=0x%x\n",i,ret&0xffff,i,0xc000+i);
	}
	
#elif(SEL_RAM == SEL_RAM3)	
	//RAM3
	for(i=0;i<(SPI_DPRAM_LOG_APWRITE_SIZE/(DPRAM_TEST_LEN>>1));i++)
	{				
		for(j=(i*(DPRAM_TEST_LEN>>1)); j<((i+1)*(DPRAM_TEST_LEN>>1)); j++)
		{
			buf_test_dpram[2*(j-(i*(DPRAM_TEST_LEN>>1)))] = (0xa000+j)>>8;
			buf_test_dpram[2*(j-(i*(DPRAM_TEST_LEN>>1)))+1] = (0xa000+j)&0xff;
			printk("buf_test_dpram[%d]=0x%x\n",j,buf_test_dpram[(j-(i*(DPRAM_TEST_LEN>>1)))]);
		}
		
		port->dpram.write_dpram(&port->dpram, ((DPRAM_TEST_LEN*i)>>1)+SPI_DPRAM_LOG_APWRITE_START, buf_test_dpram, sizeof(buf_test_dpram));
		mdelay(1);
	}
	
#elif(SEL_RAM == SEL_REG)

	port->dpram.write_ptr(&port->dpram, SPI_DPRAM_PTR0_APWRITE_BPREAD, SPI_DPRAM_PTR0_APWRITE_BPREAD);
	port->dpram.write_ptr(&port->dpram, SPI_DPRAM_PTR1_APWRITE_BPREAD, SPI_DPRAM_PTR1_APWRITE_BPREAD);
	port->dpram.write_ptr(&port->dpram, SPI_DPRAM_PTR2_APWRITE_BPREAD, SPI_DPRAM_PTR2_APWRITE_BPREAD);
	port->dpram.write_ptr(&port->dpram, SPI_DPRAM_PTR3_APWRITE_BPREAD, SPI_DPRAM_PTR3_APWRITE_BPREAD);
	port->dpram.write_mailbox(&port->dpram, SPI_DPRAM_MAILBOX_APWRITE);

	ret = port->dpram.read_ptr(&port->dpram, SPI_DPRAM_PTR0_BPWRITE_APREAD);
	if(ret != SPI_DPRAM_PTR0_BPWRITE_APREAD)
	printk("SPI_DPRAM_PTR0_BPWRITE_APREAD(0x%x)=0x%x\n",SPI_DPRAM_PTR0_BPWRITE_APREAD,ret);
	
	ret = port->dpram.read_ptr(&port->dpram, SPI_DPRAM_PTR1_BPWRITE_APREAD);
	if(ret != SPI_DPRAM_PTR1_BPWRITE_APREAD)
	printk("SPI_DPRAM_PTR1_BPWRITE_APREAD(0x%x)=0x%x\n",SPI_DPRAM_PTR1_BPWRITE_APREAD,ret);

	ret = port->dpram.read_ptr(&port->dpram, SPI_DPRAM_PTR2_BPWRITE_APREAD);
	if(ret != SPI_DPRAM_PTR2_BPWRITE_APREAD)
	printk("SPI_DPRAM_PTR2_BPWRITE_APREAD(0x%x)=0x%x\n",SPI_DPRAM_PTR2_BPWRITE_APREAD,ret);

	ret = port->dpram.read_ptr(&port->dpram, SPI_DPRAM_PTR3_BPWRITE_APREAD);
	if(ret != SPI_DPRAM_PTR3_BPWRITE_APREAD)
	printk("SPI_DPRAM_PTR3_BPWRITE_APREAD(0x%x)=0x%x\n",SPI_DPRAM_PTR3_BPWRITE_APREAD,ret);

	ret = port->dpram.read_mailbox(&port->dpram);
	if(ret != SPI_DPRAM_MAILBOX_BPWRITE)
	printk("SPI_DPRAM_MAILBOX_BPWRITE(0x%x)=0x%x\n",SPI_DPRAM_MAILBOX_BPWRITE,ret);
	

#endif


}

static void spi_testdpram_timer(unsigned long data)
{
	struct spi_fpga_port *port = (struct spi_fpga_port *)data;
	port->dpram.dpram_timer.expires  = jiffies + msecs_to_jiffies(1000);
	add_timer(&port->dpram.dpram_timer);
	//schedule_work(&port->gpio.spi_gpio_work);
	queue_work(port->dpram.spi_dpram_workqueue, &port->dpram.spi_dpram_work);
}

#endif


int spi_dpram_handle_irq(struct spi_device *spi)
{
	struct spi_fpga_port *port = spi_get_drvdata(spi);
	unsigned char mbox = port->dpram.read_mailbox(&port->dpram);
	unsigned int len;
	DBG("%s:line=%d,port=0x%x\n",__FUNCTION__,__LINE__,(int)port);
	switch(mbox)
	{
		case MAILBOX_BPWRITE_DATA:
			len = port->dpram.read_ptr(&port->dpram,SPI_DPRAM_PTR0_BPWRITE_APREAD);
			port->dpram.read_dpram(&port->dpram, SPI_DPRAM_BPWRITE_START, port->dpram.prx, len);
			port->dpram.rec_len += len;
			break;
		case MAILBOX_BPREAD_DATA:
			port->dpram.apwrite_en = TRUE;
			break;
		default:
			break;
	}
	
	return 0;
}

static int dpr_open(struct inode *inode, struct file *filp)
{
    struct spi_fpga_port *port = pFpgaPort;

	DBG("%s:line=%d,port=0x%x\n",__FUNCTION__,__LINE__,(int)port);
	filp->private_data = port;
	port->dpram.rec_len = 0;
	port->dpram.send_len = 0;
	port->dpram.apwrite_en = TRUE;

    return nonseekable_open(inode, filp);
}


static int dpr_close(struct inode *inode, struct file *filp)
{
	//struct spi_fpga_port *port = pFpgaPort;
	DBG("%s:line=%d\n",__FUNCTION__,__LINE__);
	filp->private_data = NULL;
	return 0;
}


static ssize_t dpr_read (struct file *filp, char __user *buffer, size_t count, loff_t *ppos)
{
	//int ret;
	struct spi_fpga_port *port = filp->private_data;
	DBG("%s:line=%d,port=0x%x\n",__FUNCTION__,__LINE__,(int)port);
	
	while(port->dpram.rec_len == 0)
	{
		if( filp->f_flags&O_NONBLOCK )
		return -EAGAIN;

		if(wait_event_interruptible(port->dpram.recq, (port->dpram.rec_len != 0)))
		{       
			printk("%s:NO data in dpram!\n",__FUNCTION__);
			return -ERESTARTSYS;   
		}
	}

	/*read data from buffer*/
	if(copy_to_user((char*)buffer, (char *)port->dpram.prx, port->dpram.rec_len))
    {
        printk("%s:copy_to_user err!\n",__FUNCTION__);
        return -EFAULT;
    }

	count = port->dpram.rec_len;
	port->dpram.rec_len = 0;
	
	return count;
}


static ssize_t dpr_write (struct file *filp, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct spi_fpga_port *port = filp->private_data;
	//int ret;
	DBG("%s:line=%d,port=0x%x\n",__FUNCTION__,__LINE__,(int)port); 
	
	while(port->dpram.apwrite_en == FALSE)
	{
		if(filp->f_flags & O_NONBLOCK)
            return -EAGAIN;
        if(wait_event_interruptible(port->dpram.sendq, (port->dpram.apwrite_en == TRUE)))
            return -ERESTARTSYS;
	}

	if(count > port->dpram.max_send_len)
	{
		count = port->dpram.max_send_len;
		printk("%s:count is large than max_send_len(%d),and only %d's bytes is valid!\n",__FUNCTION__,count,count);
	}
	
	if(copy_from_user((char *)port->dpram.ptx,buffer,count))
	{
		printk("%s:copy_from_user err!\n",__FUNCTION__);
		return -EFAULT;
	}

	port->dpram.write_dpram(&port->dpram, SPI_DPRAM_APWRITE_START, port->dpram.ptx, count);
	port->dpram.apwrite_en = FALSE;	//clear apwrite_en after wirte data to dpram
	port->dpram.write_mailbox(&port->dpram, MAILBOX_APSEND_IRQ);	//send irq to bp after ap write data to dpram

	return count;
    
}


unsigned int dpr_poll(struct file *filp, struct poll_table_struct * wait)
{
	unsigned int mask = 0;
	struct spi_fpga_port *port;
	port = filp->private_data;
	DBG("%s:line=%d\n",__FUNCTION__,__LINE__);

	return mask;
}


static struct file_operations dpr_fops={
	.owner=   THIS_MODULE,
	.open=    dpr_open,
	.release= dpr_close,
	.read=    dpr_read,
	.write=   dpr_write,
	.poll =   dpr_poll,
};

int spi_dpram_register(struct spi_fpga_port *port)
{
	char b[28];
	int ret;
	DBG("%s:line=%d,port=0x%x\n",__FUNCTION__,__LINE__,(int)port);
	port->dpram.prx = (char *)kzalloc(sizeof(char)*((SPI_DPRAM_BPWRITE_SIZE<<1)+6), GFP_KERNEL);
	if(port->dpram.prx == NULL)
	{
	    printk("port->dpram.prx kzalloc err!!!\n");
	    return -ENOMEM;
	}

	port->dpram.ptx = (char *)kzalloc(sizeof(char)*((SPI_DPRAM_APWRITE_SIZE<<1)+6), GFP_KERNEL);
	if(port->dpram.ptx == NULL)
	{
	    printk("port->dpram.ptx kzalloc err!!!\n");
	    return -ENOMEM;
	}
	DBG("%s:line=%d,port=0x%x\n",__FUNCTION__,__LINE__,(int)port);
	sprintf(b, "spi_dpram_busy_workqueue");
	port->dpram.spi_dpram_busy_workqueue = create_freezeable_workqueue(b);
	if (!port->dpram.spi_dpram_busy_workqueue) {
		printk("cannot create workqueue\n");
		return -EBUSY;
	}	
	INIT_WORK(&port->dpram.spi_dpram_busy_work, spi_dpram_busy_work_handler);
	
#if SPI_DPRAM_TEST
	sprintf(b, "spi_dpram_workqueue");
	port->dpram.spi_dpram_workqueue = create_freezeable_workqueue(b);
	if (!port->dpram.spi_dpram_workqueue) {
		printk("cannot create workqueue\n");
		return -EBUSY;
	}
	INIT_WORK(&port->dpram.spi_dpram_work, spi_dpram_work_handler);

	setup_timer(&port->dpram.dpram_timer, spi_testdpram_timer, (unsigned long)port);
	port->dpram.dpram_timer.expires  = jiffies+2000;//>1000ms
	add_timer(&port->dpram.dpram_timer);
#endif

	DBG("%s:line=%d,port=0x%x\n",__FUNCTION__,__LINE__,(int)port);

	//init the struct spi_dpram
	init_waitqueue_head(&port->dpram.recq);
	init_waitqueue_head(&port->dpram.sendq);
	port->dpram.rec_len = 0;
	port->dpram.send_len = 0;
	port->dpram.apwrite_en = TRUE;
	port->dpram.max_rec_len = SPI_DPRAM_BPWRITE_SIZE;
	port->dpram.max_send_len = SPI_DPRAM_APWRITE_SIZE;
	port->dpram.miscdev.minor = MISC_DYNAMIC_MINOR;
	port->dpram.miscdev.name = "spi_dpram";//spi_fpga
	port->dpram.miscdev.fops = &dpr_fops;

	ret = misc_register(&port->dpram.miscdev);
	if(ret)
	{
	    printk("misc_register err!!!\n");
	    goto err0;
	}

	port->dpram.write_dpram = spi_dpram_write_buf;
	port->dpram.read_dpram = spi_dpram_read_buf;
	port->dpram.write_ptr = spi_dpram_write_ptr;
	port->dpram.read_ptr = spi_dpram_read_ptr;
	port->dpram.write_mailbox = spi_dpram_write_mailbox;
	port->dpram.read_mailbox = spi_dpram_read_mailbox;
	
	DBG("%s:line=%d,port=0x%x\n",__FUNCTION__,__LINE__,(int)port);

	ret = gpio_request(SPI_DPRAM_BUSY_PIN, NULL);
	if (ret) {
		printk("%s:failed to request fpga busy gpio\n",__FUNCTION__);
		goto err1;
	}

	gpio_pull_updown(SPI_DPRAM_BUSY_PIN,GPIOPullUp);
	ret = request_irq(gpio_to_irq(SPI_DPRAM_BUSY_PIN),spi_dpram_busy_irq,IRQF_TRIGGER_RISING,NULL,port);
	if(ret)
	{
		printk("unable to request fpga busy_gpio irq\n");
		goto err2;
	}	
	DBG("%s:line=%d,port=0x%x\n",__FUNCTION__,__LINE__,(int)port);
	
	return 0;
	
err2:
	free_irq(gpio_to_irq(SPI_DPRAM_BUSY_PIN),NULL);
err1:	
	gpio_free(SPI_DPRAM_BUSY_PIN);
err0:
	kfree(port->dpram.prx);
	kfree(port->dpram.ptx);

	return ret;
	
}

int spi_dpram_unregister(struct spi_fpga_port *port)
{

	return 0;
}



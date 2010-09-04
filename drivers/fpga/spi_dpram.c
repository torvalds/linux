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


#define SPI_DPRAM_PTR0_BPWRITE_APREAD	0X3fee
#define	SPI_DPRAM_PTR0_APWRITE_BPREAD	0X3ff0

#define SPI_DPRAM_PTR1_BPWRITE_APREAD	0x3ff2
#define	SPI_DPRAM_PTR1_APWRITE_BPREAD	0x3ff4

#define SPI_DPRAM_PTR2_BPWRITE_APREAD	0x3ff6
#define	SPI_DPRAM_PTR2_APWRITE_BPREAD	0x3ff8

#define SPI_DPRAM_PTR3_BPWRITE_APREAD	0x3ffa
#define	SPI_DPRAM_PTR3_APWRITE_BPREAD	0x3ffc

#define SPI_DPRAM_MAILBOX_BPIRQ		0x3ffe
#define SPI_DPRAM_MAILBOX_APIRQ		0x3fff
#define SPI_DPRAM_MAILBOX_BPACK		0x3ffb
#define SPI_DPRAM_MAILBOX_APACK		0x3ffd

/*mailbox comminication's definition*/
#define MAILBOX_BPSEND_IRQ	(0<<15)
#define MAILBOX_BPSEND_ACK	(1<<15)
#define MAILBOX_APSEND_IRQ	(0<<15)
#define MAILBOX_APSEND_ACK	(1<<15)
#define MAILBOX_RAM0		(0<<13)
#define MAILBOX_RAM1		(1<<13)
#define MAILBOX_RAM2		(2<<13)
#define MAILBOX_RAM3		(3<<13)

#define PIN_BPSEND_ACK	RK2818_PIN_PE0
#define PIN_APSEND_ACK	RK2818_PIN_PF7

#define MAX_SPI_LEN	512		//the bytes of spi write or read one time


static int spi_dpram_write_buf(struct spi_dpram *dpram, unsigned short int addr, unsigned char *buf, unsigned int len)
{	
	struct spi_fpga_port *port = container_of(dpram, struct spi_fpga_port, dpram);
	unsigned char opt = ((ICE_SEL_DPRAM & ICE_SEL_DPRAM_NOMAL) | ICE_SEL_DPRAM_WRITE);
	unsigned char tx_buf[MAX_SPI_LEN+3];
	int i,ret,mod,num,count;

#if 0
	unsigned char *p = buf;
	for(i=0;i<len;i++)
	{
		DBG("%s:buf[%d]=0x%x\n",__FUNCTION__,i,*p);
		p++;
	}
#endif

	mod = len%MAX_SPI_LEN;
	if(!mod)
		num = len/MAX_SPI_LEN;
	else
		num = len/MAX_SPI_LEN + 1;

	for(i=0;i<num;i++)
	{	
		if(i == num -1)
		{
			if(!mod)
				count = MAX_SPI_LEN;
			else
				count = mod;			
			memcpy(tx_buf + 3, buf+i*MAX_SPI_LEN, count);
		}
		else
		{
			count = MAX_SPI_LEN;
			memcpy(tx_buf + 3, buf+i*MAX_SPI_LEN, count);
		}

		tx_buf[0] = opt;
		tx_buf[1] = (((addr + i*(MAX_SPI_LEN>>1)) << 1) >> 8) & 0xff;
		tx_buf[2] = (((addr + i*(MAX_SPI_LEN>>1)) << 1) & 0xff);
		ret = spi_write(port->spi, tx_buf, count+3);
		if(ret)
		{
			DBG("%s:spi_write err! i=%d\n",__FUNCTION__,i);
			return ret;
		}
	}
	
	return 0;
}

static int spi_dpram_read_buf(struct spi_dpram *dpram, unsigned short int addr, unsigned char *buf, unsigned int len)
{
	struct spi_fpga_port *port = container_of(dpram, struct spi_fpga_port, dpram);
	unsigned char opt = ((ICE_SEL_DPRAM & ICE_SEL_DPRAM_NOMAL & ICE_SEL_DPRAM_READ));
	unsigned char tx_buf[4];
	unsigned char stat;
	int i,mod,num,count;
	
	mod = len%MAX_SPI_LEN;
	if(!mod)
		num = len/MAX_SPI_LEN;
	else
		num = len/MAX_SPI_LEN + 1;

	for(i=0;i<num;i++)
	{
		if(i == num -1)
		{
			if(!mod)
				count = MAX_SPI_LEN;
			else
				count = mod;			
		}
		else
		{
			count = MAX_SPI_LEN;
		}
		
		tx_buf[0] = opt;
		tx_buf[1] = (((addr + i*(MAX_SPI_LEN>>1)) << 1) >> 8) & 0xff;
		tx_buf[2] = (((addr + i*(MAX_SPI_LEN>>1)) << 1) & 0xff);
		tx_buf[3] = 0;//give fpga 8 clks for reading data

		stat = spi_write_then_read(port->spi, tx_buf, sizeof(tx_buf), buf + i*MAX_SPI_LEN, count);	
		if(stat)
		{
			DBG("%s:spi_write_then_read is error!,err=%d\n\n",__FUNCTION__,stat);
			return -1;
		}

	}
	
	return 0;

}


int spi_dpram_write_word(struct spi_dpram *dpram, unsigned short int addr, unsigned int size)
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
		DBG("%s:spi_write err!\n",__FUNCTION__);
		return -1;
	}
	
	return 0;

}


int spi_dpram_read_word(struct spi_dpram *dpram, unsigned short int addr)
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
		DBG("%s:spi_write_then_read err!\n",__FUNCTION__);
		return -1;
	}
	
	ret = (rx_buf[0] << 8) | rx_buf[1];
	
	return (ret&0xffff);

}

int spi_dpram_write_ptr(struct spi_dpram *dpram, unsigned short int addr, unsigned int size)
{
	return spi_dpram_write_word(dpram, addr, size);
}


int spi_dpram_read_ptr(struct spi_dpram *dpram, unsigned short int addr)
{
	return spi_dpram_read_word(dpram, addr);
}


int spi_dpram_write_irq(struct spi_dpram *dpram, unsigned int mailbox)
{
	return spi_dpram_write_word(dpram, SPI_DPRAM_MAILBOX_APIRQ,mailbox);
}


int spi_dpram_read_irq(struct spi_dpram *dpram)
{
	return  spi_dpram_read_word(dpram, SPI_DPRAM_MAILBOX_BPIRQ);
}

int spi_dpram_write_ack(struct spi_dpram *dpram, unsigned int mailbox)
{
	return spi_dpram_write_word(dpram, SPI_DPRAM_MAILBOX_APACK,mailbox);
}


int spi_dpram_read_ack(struct spi_dpram *dpram)
{
	return  spi_dpram_read_word(dpram, SPI_DPRAM_MAILBOX_BPACK);
}

int gNumSendInt=0,gLastNumSendInt = 0;
int gNumSendAck=0,gLastNumSendAck = 0;
int gNumRecInt=0,gNumLastRecInt = 0;
int gNumCount = 0;
unsigned char buf_dpram[SPI_DPRAM_BPWRITE_SIZE<<1];

int gRecCount = 0; 

//really is spi_dpram_irq_work_handler after dpram's pin is exchanged 
static void spi_dpram_irq_work_handler(struct work_struct *work)
{
	struct spi_fpga_port *port =
		container_of(work, struct spi_fpga_port, dpram.spi_dpram_irq_work);
	
	unsigned short int mbox = port->dpram.read_irq(&port->dpram);
	unsigned int ptr,len;
	int i;
	char temp,*p;
	DBG("Enter::%s,mbox=%d\n",__FUNCTION__,mbox);
	//gNumRecInt = mbox & 0x1fff;
	//if(gNumRecInt - gNumLastRecInt !=1)
	//if(++gNumLastRecInt > (1<<12))
	//gNumLastRecInt = 0;	
	//printk("gNumRecInt=%d,gLastNumInt=%d\n",gNumRecInt,gNumLastRecInt);
	
	if((mbox&MAILBOX_RAM3) == MAILBOX_RAM0)
	{		
		//RAM0
		ptr = port->dpram.read_ptr(&port->dpram,SPI_DPRAM_PTR0_BPWRITE_APREAD);
		if(ptr%2)
		len = ptr+1;
		else
		len = ptr;		
		port->dpram.read_dpram(&port->dpram, SPI_DPRAM_BPWRITE_START, port->dpram.prx, len);
		port->dpram.rec_len += ptr;	
		gRecCount = port->dpram.rec_len; 
		DBG("%s:ram0:ptr=%d,len=%d\n",__FUNCTION__,ptr,len);	
		//send ack
		//if(++gNumSendAck > (1<<12))
		//gNumSendAck = 0;
		//port->dpram.write_ack(&port->dpram, (MAILBOX_APSEND_ACK | MAILBOX_RAM0 | gNumSendAck));

		p = port->dpram.prx;
		for(i=0;i<(len>>1);i++)
		{
			temp = *(p+(i<<1));
			*(p+(i<<1))= *(p+(i<<1)+1);
			*(p+(i<<1)+1) = temp;
		}
		
	p = port->dpram.prx;
	for(i=0;i<ptr;i++)
		printk("%s:prx[%d]=0x%x\n",__FUNCTION__,i,*p++);
		
		//wake up ap to read data
		wake_up_interruptible(&port->dpram.recq);

		mutex_lock(&port->dpram.rec_lock);

		//allow bp write ram0 again
		port->dpram.write_ack(&port->dpram, (MAILBOX_APSEND_ACK | MAILBOX_RAM0));
		
		//DBG("%s:r_irq=0x%x,s_ack=0x%x\n",__FUNCTION__,mbox, (MAILBOX_APSEND_ACK | MAILBOX_RAM0 | gNumSendAck));
	}
	else if((mbox&MAILBOX_RAM3) == MAILBOX_RAM2)
	{
		//RAM2
		ptr = port->dpram.read_ptr(&port->dpram,SPI_DPRAM_PTR2_BPWRITE_APREAD);
		if(ptr%2)
		len = ptr+1;
		else
		len = ptr;	
		port->dpram.read_dpram(&port->dpram, SPI_DPRAM_LOG_BPWRITE_START, port->dpram.prx, len);
		port->dpram.rec_len += ptr;	
		DBG("%s:ram2:ptr=%d,len=%d\n",__FUNCTION__,ptr,len);	
		//if(++gNumSendAck > (1<<12))
		//gNumSendAck = 0;
		//port->dpram.write_ack(&port->dpram, (MAILBOX_APSEND_ACK | MAILBOX_RAM2 | gNumSendAck));

		p = port->dpram.prx;
		for(i=0;i<(len>>1);i++)
		{
			temp = *(p+(i<<1));
			*(p+(i<<1))= *(p+(i<<1)+1);
			*(p+(i<<1)+1) = temp;
		}
		
		//wake up ap to read data
		wake_up_interruptible(&port->dpram.recq);

		mutex_lock(&port->dpram.rec_lock);

		//allow bp write ram0 again
		port->dpram.write_ack(&port->dpram, (MAILBOX_APSEND_ACK | MAILBOX_RAM2));
		
		//DBG("%s:r_irq=0x%x,s_ack=0x%x\n",__FUNCTION__, mbox, (MAILBOX_APSEND_ACK | MAILBOX_RAM2 | gNumSendAck));
	} 
	
}

static irqreturn_t spi_dpram_irq(int irq, void *dev_id)
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

	queue_work(port->dpram.spi_dpram_irq_workqueue, &port->dpram.spi_dpram_irq_work);
	
	return IRQ_HANDLED;
}


#if SPI_DPRAM_TEST
#define SEL_RAM0	0
#define SEL_RAM1	1
#define SEL_RAM2	2
#define SEL_RAM3	3
#define SEL_REG		4
#define SEL_RAM		SEL_REG

void spi_dpram_work_handler(struct work_struct *work)
{
	int i;
	struct spi_fpga_port *port =
		container_of(work, struct spi_fpga_port, dpram.spi_dpram_work);
	DBG("*************test spi_dpram now***************\n");

	for(i=0;i<SPI_DPRAM_BPWRITE_SIZE;i++)
	{
		buf_dpram[2*i] = (0xa000+i)>>8;
		buf_dpram[2*i+1] = (0xa000+i)&0xff;
	}
	
#if(SEL_RAM == SEL_RAM0)
	//RAM0
	port->dpram.read_dpram(&port->dpram, SPI_DPRAM_BPWRITE_START, port->dpram.prx, SPI_DPRAM_BPWRITE_SIZE<<1);
	for(i=0;i<SPI_DPRAM_BPWRITE_SIZE;i++)
	{
		ret = (*(port->dpram.prx+2*i)<<8) | (*(port->dpram.prx+2*i+1));
		if(ret != 0xa000+i)
		DBG("prx[%d]=0x%x ram[%d]=0x%x\n",i,ret&0xffff,i,0xa000+i);
	}
	
#elif(SEL_RAM == SEL_RAM1)	
	//RAM1
	port->dpram.write_dpram(&port->dpram, SPI_DPRAM_APWRITE_START, buf_dpram, SPI_DPRAM_APWRITE_SIZE<<1);
	port->dpram.write_ptr(&port->dpram, SPI_DPRAM_PTR1_APWRITE_BPREAD, SPI_DPRAM_APWRITE_START);
	port->dpram.write_irq(&port->dpram, MAILBOX_APSEND_IRQ);	//send irq to bp after ap write data to dpram

#elif(SEL_RAM == SEL_RAM2)
	//RAM2
	port->dpram.read_dpram(&port->dpram, SPI_DPRAM_LOG_BPWRITE_START, port->dpram.prx, SPI_DPRAM_LOG_BPWRITE_SIZE<<1);	
	for(i=0;i<SPI_DPRAM_LOG_BPWRITE_SIZE;i++)
	{
		ret = (*(port->dpram.prx+2*i)<<8) | (*(port->dpram.prx+2*i+1));
		if(ret != 0xc000+i)
		DBG("prx[%d]=0x%x ram[%d]=0x%x\n",i,ret&0xffff,i,0xc000+i);
	}

#elif(SEL_RAM == SEL_RAM3)	
	//RAM3
	port->dpram.write_dpram(&port->dpram, SPI_DPRAM_LOG_APWRITE_START, buf_dpram, SPI_DPRAM_LOG_APWRITE_SIZE<<1);
	port->dpram.write_ptr(&port->dpram, SPI_DPRAM_PTR3_APWRITE_BPREAD, SPI_DPRAM_LOG_APWRITE_START);
	port->dpram.write_irq(&port->dpram, MAILBOX_APSEND_IRQ);	//send irq to bp after ap write data to dpram
	
#elif(SEL_RAM == SEL_REG)
#if 1
	if(gNumCount++ == 0)
	{
		if(++gNumSendAck > (1<<12))
		gNumSendAck = 0;
		port->dpram.write_ack(&port->dpram, MAILBOX_APSEND_ACK | MAILBOX_RAM0 | gNumSendAck);
		printk("%s:line=%d,s_ack=0x%x\n",__FUNCTION__,__LINE__,MAILBOX_APSEND_ACK | MAILBOX_RAM0 | gNumSendAck);

		while(port->dpram.apwrite_en != TRUE);
		port->dpram.apwrite_en = FALSE;
		if(++gNumSendInt > (1<<12))
		gNumSendInt = 0;
		port->dpram.write_dpram(&port->dpram, SPI_DPRAM_APWRITE_START, buf_dpram, SPI_DPRAM_APWRITE_SIZE<<1);
		port->dpram.write_ptr(&port->dpram, SPI_DPRAM_PTR1_APWRITE_BPREAD, SPI_DPRAM_APWRITE_SIZE<<1);
		port->dpram.write_irq(&port->dpram, MAILBOX_APSEND_IRQ | MAILBOX_RAM1 | gNumSendInt);
		printk("%s:line=%d,s_irq=0x%x\n",__FUNCTION__,__LINE__,MAILBOX_APSEND_IRQ | MAILBOX_RAM1 | gNumSendInt);

		if(gNumCount > (1<<15))
		gNumCount = 2;
	}
#endif

#if 0	
	while(port->dpram.apwrite_en != TRUE);
	port->dpram.apwrite_en == FALSE;
	if(++gNumSendInt > (1<<12))
	gNumSendInt = 0;
	port->dpram.write_dpram(&port->dpram, SPI_DPRAM_LOG_APWRITE_START, buf_dpram, SPI_DPRAM_LOG_APWRITE_SIZE<<1);
	port->dpram.write_ptr(&port->dpram, SPI_DPRAM_PTR3_APWRITE_BPREAD, SPI_DPRAM_LOG_APWRITE_SIZE<<1);
	port->dpram.write_irq(&port->dpram, MAILBOX_APSEND_IRQ | MAILBOX_RAM3 | gNumSendInt);
#endif
#endif

}

static void spi_testdpram_timer(unsigned long data)
{
	struct spi_fpga_port *port = (struct spi_fpga_port *)data;
	port->dpram.dpram_timer.expires  = jiffies + msecs_to_jiffies(500);
	add_timer(&port->dpram.dpram_timer);
	//schedule_work(&port->gpio.spi_gpio_work);
	queue_work(port->dpram.spi_dpram_workqueue, &port->dpram.spi_dpram_work);
}

#endif

//really is spi_dpram_handle_ack after dpram's pin is exchanged 
int spi_dpram_handle_ack(struct spi_device *spi)
{
	struct spi_fpga_port *port = spi_get_drvdata(spi);
	printk("Enter::%s,LINE=%d\n",__FUNCTION__,__LINE__);
	//clear ack interrupt
	port->dpram.read_ack(&port->dpram);
	
	//allow ap to write and wake ap to write data
	port->dpram.apwrite_en = TRUE;	
	wake_up_interruptible(&port->dpram.sendq);
#if 0
	//while(port->dpram.apwrite_en != TRUE);
	port->dpram.apwrite_en = FALSE;
	if(++gNumSendInt > (1<<12))
	gNumSendInt = 0;
	port->dpram.write_dpram(&port->dpram, SPI_DPRAM_APWRITE_START, buf_dpram, SPI_DPRAM_APWRITE_SIZE<<1);
	port->dpram.write_ptr(&port->dpram, SPI_DPRAM_PTR1_APWRITE_BPREAD, SPI_DPRAM_APWRITE_SIZE<<1);
	port->dpram.write_irq(&port->dpram, MAILBOX_APSEND_IRQ | MAILBOX_RAM1 | gNumSendInt);
	printk("%s:r_ack=0x%x,s_irq=0x%x\n",__FUNCTION__,ack, MAILBOX_APSEND_IRQ | MAILBOX_RAM1 | gNumSendInt);
#endif
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
	int ret;
	struct spi_fpga_port *port = filp->private_data;

	ret = down_interruptible(&port->dpram.rec_sem);
	//DBG("%s:line=%d,port=0x%x\n",__FUNCTION__,__LINE__,(int)port); 
	DBG("%s, port=0x%x , count=%d, port->dpram.rec_len=%d \n",__FUNCTION__, (int)port, count, port->dpram.rec_len); 
		
	//printk("%s:CURRENT_TASK=0x%x\n",__FUNCTION__,current);	
	while(port->dpram.rec_len == 0)
	{
		if( filp->f_flags&O_NONBLOCK )
		return -EAGAIN;

		if(wait_event_interruptible(port->dpram.recq, (port->dpram.rec_len != 0)))
		{       
			DBG("%s:NO data in dpram!\n",__FUNCTION__);
			return -ERESTARTSYS;   
		}

		//gRecCount = port->dpram.rec_len;
	}

	/*read data from buffer*/
	if(copy_to_user((char*)buffer, (char *)(port->dpram.prx + gRecCount - port->dpram.rec_len), count))
    {
        DBG("%s:copy_to_user err!\n",__FUNCTION__);
        return -EFAULT;
    }
	
	#if 1 
	int i,len;
	char *p = buffer;
	len = port->dpram.rec_len;
	//for(i=0;i<len;i++)
	//{
	if(count==1){
		DBG("%s:prx[%d]=0x%x, src = %x \n",__FUNCTION__,i,*p, *((char *)(port->dpram.prx + gRecCount - port->dpram.rec_len)));
	}else{
		printk("count = %d, $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n", count); 
	}
	#endif
	
	port->dpram.rec_len -= count;
	if(port->dpram.rec_len == 0)
	mutex_unlock(&port->dpram.rec_lock);	

	up(&port->dpram.rec_sem);

	return count;
}


static ssize_t dpr_write (struct file *filp, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct spi_fpga_port *port = filp->private_data;
	int mod,num;
	char i,*p,temp;
	printk("%s:line=%d,port=0x%x, count=%d \n",__FUNCTION__,__LINE__,(int)port, count); 
	
	while(port->dpram.apwrite_en == FALSE)
	{
		
		//if(filp->f_flags & O_NONBLOCK)
    //        return -EAGAIN;
        if(wait_event_interruptible(port->dpram.sendq, (port->dpram.apwrite_en == TRUE))){ 
        		printk("port->dpram.apwrite_en == FALSE"); 
            return -ERESTARTSYS; 
        }
		printk("%s, wake up \n", __FUNCTION__); 
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
	mod = count % 2;
	num = count;
	if(mod)
	{
		*((char *)port->dpram.ptx + count) = 0;
		num = count + 1;
	}
	
	p=port->dpram.ptx;
	
	/*swap data to suitable bp:p[0]<->p[1]*/
	for(i=0;i<(num>>1);i++)
	{
		temp = *(p+(i<<1));
		*(p+(i<<1))= *(p+(i<<1)+1);
		*(p+(i<<1)+1) = temp;
	}
	
#if 1
	p=port->dpram.ptx;
	for(i=0;i<num;i++)
	{
		/*DBG*/printk("%s:ptx[%d]=0x%x\n",__FUNCTION__,i,*p);
		p++;
	}
#endif
	
	port->dpram.write_dpram(&port->dpram, SPI_DPRAM_APWRITE_START, port->dpram.ptx, num);
	port->dpram.apwrite_en = FALSE;	//clear apwrite_en after wirte data to dpram
	port->dpram.write_ptr(&port->dpram, SPI_DPRAM_PTR1_APWRITE_BPREAD, count);
	if(++gNumSendInt > (1<<12))
	gNumSendInt = 0;

	if(gpio_get_value(PIN_BPSEND_ACK)==0){ 
		printk("BP_READY is LOW, wake up BP \n"); 
		gpio_direction_output(PIN_APSEND_ACK,GPIO_LOW);
		msleep(50); 
		gpio_direction_output(PIN_APSEND_ACK,GPIO_HIGH);
		msleep(50); 
	} 
	
	while(gpio_get_value(PIN_BPSEND_ACK)==0){ 
		printk("BP_READY is LOW, wait 100ms !!!!!!!!!!!!\n"); 
		msleep(100); 
	} 

	//send irq to bp after ap write data to dpram
	port->dpram.write_irq(&port->dpram, MAILBOX_APSEND_IRQ | MAILBOX_RAM1 | gNumSendInt);	

	return count;
    
}


unsigned int dpr_poll(struct file *filp, struct poll_table_struct * wait)
{
	unsigned int mask = 0;
	struct spi_fpga_port *port;
	port = filp->private_data;
	DBG("%s:line=%d\n",__FUNCTION__,__LINE__);

#if 1
	poll_wait(filp, &port->dpram.recq, wait);
	poll_wait(filp, &port->dpram.sendq, wait);

	if(port->dpram.rec_len >0)
	{
	    mask |= POLLIN|POLLRDNORM;
	    DBG("%s:exsram_poll_____1\n",__FUNCTION__);
	}
	
	if(port->dpram.apwrite_en == TRUE)
	{
	    mask |= POLLOUT|POLLWRNORM;
		DBG("%s:exsram_poll_____2\n",__FUNCTION__);
	}
#endif

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
	    DBG("port->dpram.prx kzalloc err!!!\n");
	    return -ENOMEM;
	}

	port->dpram.ptx = (char *)kzalloc(sizeof(char)*((SPI_DPRAM_APWRITE_SIZE<<1)+6), GFP_KERNEL);
	if(port->dpram.ptx == NULL)
	{
	    DBG("port->dpram.ptx kzalloc err!!!\n");
	    return -ENOMEM;
	}
	DBG("%s:line=%d,port=0x%x\n",__FUNCTION__,__LINE__,(int)port);
	sprintf(b, "spi_dpram_irq_workqueue");
	port->dpram.spi_dpram_irq_workqueue = create_freezeable_workqueue(b);
	if (!port->dpram.spi_dpram_irq_workqueue) {
		DBG("cannot create workqueue\n");
		return -EBUSY;
	}	
	INIT_WORK(&port->dpram.spi_dpram_irq_work, spi_dpram_irq_work_handler);
	
#if SPI_DPRAM_TEST
	sprintf(b, "spi_dpram_workqueue");
	port->dpram.spi_dpram_workqueue = create_freezeable_workqueue(b);
	if (!port->dpram.spi_dpram_workqueue) {
		DBG("cannot create workqueue\n");
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
	mutex_init(&port->dpram.rec_lock);
	mutex_init(&port->dpram.send_lock);
	init_MUTEX(&port->dpram.rec_sem);
	init_MUTEX(&port->dpram.send_sem);
	spin_lock_init(&port->dpram.spin_rec_lock);
	spin_lock_init(&port->dpram.spin_send_lock);
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
	    DBG("%s:misc_register err!!!\n",__FUNCTION__);
	    goto err0;
	}

	port->dpram.write_dpram = spi_dpram_write_buf;
	port->dpram.read_dpram = spi_dpram_read_buf;
	port->dpram.write_ptr = spi_dpram_write_ptr;
	port->dpram.read_ptr = spi_dpram_read_ptr;
	port->dpram.write_irq = spi_dpram_write_irq;
	port->dpram.read_irq = spi_dpram_read_irq;
	port->dpram.write_ack = spi_dpram_write_ack;
	port->dpram.read_ack = spi_dpram_read_ack;
	
	DBG("%s:line=%d,port=0x%x\n",__FUNCTION__,__LINE__,(int)port);

	ret = gpio_request(SPI_DPRAM_INT_PIN, NULL);
	if (ret) {
		DBG("%s:failed to request dpram irq gpio\n",__FUNCTION__);
		goto err1;
	}

	rk2818_mux_api_set(GPIOA23_UART2_SEL_NAME,0);
	gpio_pull_updown(SPI_DPRAM_INT_PIN,GPIOPullUp);
	ret = request_irq(gpio_to_irq(SPI_DPRAM_INT_PIN),spi_dpram_irq,IRQF_TRIGGER_FALLING,NULL,port);
	if(ret)
	{
		DBG("%s:unable to request dpram irq_gpio irq\n",__FUNCTION__);
		goto err2;
	}	
	DBG("%s:line=%d,port=0x%x\n",__FUNCTION__,__LINE__,(int)port);

	
	/*disable speaker */
	gpio_request(RK2818_PIN_PF7, "DPRAM"); 
	rk2818_mux_api_set(GPIOE_SPI1_FLASH_SEL_NAME, IOMUXA_GPIO1_A3B7);
	gpio_direction_output(RK2818_PIN_PF7,GPIO_LOW);
	msleep(100); 
	gpio_direction_output(RK2818_PIN_PF7,GPIO_HIGH);
	msleep(100); 
	
	return 0;
	
err2:
	free_irq(gpio_to_irq(SPI_DPRAM_INT_PIN),NULL);
err1:	
	gpio_free(SPI_DPRAM_INT_PIN);
err0:
	kfree(port->dpram.prx);
	kfree(port->dpram.ptx);

	return ret;
	
}

int spi_dpram_unregister(struct spi_fpga_port *port)
{

	return 0;
}



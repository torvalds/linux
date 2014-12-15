#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/slab.h>
//#include <linux/io.h>
#include <plat/io.h>
#include <linux/bitops.h>
#include <linux/crc32.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/kthread.h>
#include <asm/irqflags.h>
#include <mach/meson-secure.h>

#define SECURE_MONITOR_MODULE_NAME "secure_monitor"
#define SECURE_MONITOR_DRIVER_NAME  "secure_monitor"
#define SECURE_MONITOR_DEVICE_NAME  "secure_monitor"
#define SECURE_MONITOR_CLASS_NAME "secure_monitor"

/* 
  * SHARE MEM:
  * Communicate Head: 1KB
  * communicate Head lock mutex location: the last 4B of HEAD; inited by secureOS
  * Data: 128KB
*/
#define SHARE_MEM_HEAD_OFFSET 0x0
#define SHARE_MEM_DATA_OFFSET 0x400
#define SHARE_MEM_PHY_SIZE 0x20400
#define FLASH_BUF_SIZE 0x20000

#define SHARE_MEM_CMD_FREE	0x00000000
#define SHARE_MEM_CMD_WRITE	0x00000001

struct __NS_SHARE_MEM_HEAD {
	unsigned int cmdlock;
	unsigned int cmd;	
	unsigned int state;	
	unsigned int input;   // can store input data position in NS_SHARE_MEM_CONTENT
	unsigned int inlen;
	unsigned int output; 
	unsigned int outlen;
};
typedef struct __NS_SHARE_MEM_HEAD  NS_SHARE_MEM_HEAD;

struct secure_monitor_arg{
	unsigned char* pfbuf;
	unsigned char*psbuf;
};
static struct secure_monitor_arg secure_monitor_buf;
static struct task_struct *secure_task = NULL;

static int secure_writer_monitor(void *arg);
extern void lock_mutex(unsigned int* lock);
extern void init_mutex(unsigned int* lock);
extern unsigned int unlock_mutex(unsigned int* lock);
extern void write_to_flash(unsigned char* psrc, unsigned size);

static int secure_monitor_start(void)
{	
	int ret=0;
	printk("%s:%d\n", __FUNCTION__, __LINE__);
	secure_monitor_buf.pfbuf = kmalloc(FLASH_BUF_SIZE, GFP_KERNEL);
	if(!secure_monitor_buf.pfbuf){
		printk("nandbuf create fail!\n");
		ret = -ENOMEM;
		goto flash_monitor_probe_exit;		
	}
	secure_monitor_buf.psbuf = ioremap_cached(meson_secure_mem_flash_start(), meson_secure_mem_flash_size());
	if(!secure_monitor_buf.psbuf){
		printk("ioremap share memory fail \n");
		ret = -ENOMEM;
		goto flash_monitor_probe_exit1;				
	}
	
	secure_task = kthread_run(secure_writer_monitor, (void*)(&secure_monitor_buf), "secure_flash");
	if(!secure_task){
		printk("create secure task failed \n");
		ret = -ENODEV;
		goto flash_monitor_probe_exit2;				
	}	
	goto flash_monitor_probe_exit;
	
flash_monitor_probe_exit2:
	if(secure_monitor_buf.psbuf)
		iounmap(secure_monitor_buf.psbuf);
	secure_monitor_buf.psbuf = NULL;
		
flash_monitor_probe_exit1:
	if(secure_monitor_buf.pfbuf)
		kfree(secure_monitor_buf.pfbuf);
	secure_monitor_buf.pfbuf = NULL;	

flash_monitor_probe_exit:		
	return ret;
}

void write_to_flash(unsigned char* psrc, unsigned size)
{	
#ifdef CONFIG_SECURE_NAND
extern int aml_nand_save_secure(struct mtd_info *mtd, u_char *buf, unsigned int size);
extern struct mtd_info * nand_secure_mtd;

	unsigned char * secure_ptr = psrc;
	int error = 0;
	printk("%s %d save secure here \n",__func__, __LINE__);
	error = aml_nand_save_secure(nand_secure_mtd, secure_ptr, size);
	if(error){
		printk("save secure failed\n");
	}	
	printk("///////////////////////////////////////save secure success//////////////////////////////////\n");
	return;
	
#elif CONFIG_SPI_NOR_SECURE_STORAGE
extern 	int secure_storage_spi_write(u8 *buf,u32 len);
	unsigned char * secure_ptr = psrc;
	int error = 0;
		
	printk("%s %d save secure here \n",__func__, __LINE__);
	error=secure_storage_spi_write(secure_ptr, size);
	if(error){
		printk("save secure failed\n");
	}	
	printk("///////////////////////////////////////save secure success//////////////////////////////////\n");
	return;
#endif

#ifdef CONFIG_EMMC_SECURE_STORAGE
extern 	int mmc_secure_storage_ops(unsigned char * buf, unsigned int len, int wr_flag);
	unsigned char * secure_ptr = psrc;
	int error = 0;

	printk("%s %d save secure here \n",__func__, __LINE__);
	error=mmc_secure_storage_ops(secure_ptr, size, 1);
	if(error){
		printk("save secure failed\n");
	}
	printk("///////////////////////////////////////save secure success//////////////////////////////////\n");
	return;
#endif

}


static int secure_writer_monitor(void *arg)
{
	struct secure_monitor_arg *parg = (struct secure_monitor_arg*)arg;
	unsigned char *pfbuf = parg->pfbuf;	
	unsigned long flags;
	
	NS_SHARE_MEM_HEAD *pshead = (NS_SHARE_MEM_HEAD*)(parg->psbuf+SHARE_MEM_HEAD_OFFSET);
	unsigned char *psdata = parg->psbuf + SHARE_MEM_DATA_OFFSET;
	bool w_busy = false;	
		
	while(1){
			if(w_busy){
				write_to_flash(pfbuf, FLASH_BUF_SIZE);
				w_busy=false;
			}						
			//arch_local_irq_save();
			local_fiq_disable();
			lock_mutex(&(pshead->cmdlock));						
			if(pshead->cmd == SHARE_MEM_CMD_WRITE){				
				memcpy(pfbuf, psdata, FLASH_BUF_SIZE);
				pshead->cmd=SHARE_MEM_CMD_FREE;
				w_busy = true;
				printk("************kernel detect write flag*****\n");
			}
			unlock_mutex(&(pshead->cmdlock));		
			//arch_local_irq_restore(flags);
			local_fiq_enable();			
			
			if(!w_busy){				
				if(kthread_should_stop())
					break;
				else
					msleep(200);			
			}
	}	
	return 0;
}

static int __init secure_monitor_init(void)
{
	int ret=-1;
	printk("%s:%d\n", __FUNCTION__, __LINE__);
	ret = secure_monitor_start();
	if(ret != 0){
		printk(KERN_ERR "failed to register flash monitor driver, error %d\n", ret);
		return -ENODEV;
	}
	return ret;		
}

static void __exit secure_monitor_exit(void)
{
	int ret=0;
	printk("**************flash_secure_remove start!\n");
	if(secure_task){
		kthread_stop(secure_task);
		secure_task = NULL;
	}

	if(secure_monitor_buf.psbuf)
		iounmap(secure_monitor_buf.psbuf);
	secure_monitor_buf.psbuf = NULL;

	if(secure_monitor_buf.pfbuf)
		kfree(secure_monitor_buf.pfbuf);
	secure_monitor_buf.pfbuf = NULL;

	printk("**************flash_secure_remove end!\n");
	return ret;
}

module_init(secure_monitor_init);
module_exit(secure_monitor_exit);


MODULE_DESCRIPTION("AMLOGIC secure monitor driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yan Wang <yan.wang@amlogic.com>");

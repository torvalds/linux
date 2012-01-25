/*
 * cedar_dev.c
 * aw16xx cedar video engine driver
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/preempt.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/rmap.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <linux/sched.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <mach/hardware.h>
#include <asm/system.h>

#include "sun3i_cedar.h"

#define DRV_VERSION "0.01alpha"

#define CHIP_VERSION_F20

#ifndef CEDARDEV_MAJOR
#define CEDARDEV_MAJOR (150)
#endif
#ifndef CEDARDEV_MINOR
#define CEDARDEV_MINOR (0)
#endif

#undef cdebug
#ifdef CEDAR_DEBUG
#  define cdebug(fmt, args...) printk( KERN_DEBUG "[cedar]: " fmt, ## args)
#else
#  define cdebug(fmt, args...)
#endif

int g_dev_major = CEDARDEV_MAJOR;
int g_dev_minor = CEDARDEV_MINOR;
module_param(g_dev_major, int, S_IRUGO);
module_param(g_dev_minor, int, S_IRUGO);

#define VE_IRQ_NO (48)

#define CCMU_VE_PLL_REG      (0xf1c20000)
#define CCMU_AHB_GATE_REG    (0xf1c2000c)
#define CCMU_SDRAM_PLL_REG   (0xf1c20020)
#define CCMU_VE_CLK_REG      (0xf1c20028)
#define CCMU_TS_SS_CLK_REG   (0xf1c2003c)
#define CCMU_AHB_APB_CFG_REG (0xf1c20008)

#define SRAM_MASTER_CFG_REG  (0xf1c01060)
#define CPU_SRAM_PRIORITY_REG    (SRAM_MASTER_CFG_REG + 0x00)
#define VE_SRAM_PRIORITY_REG     (SRAM_MASTER_CFG_REG + 0x20)
#define IMAGE0_SRAM_PRIORITY_REG (SRAM_MASTER_CFG_REG + 0x10)
#define SCALE0_SRAM_PRIORITY_REG (SRAM_MASTER_CFG_REG + 0x14)
#define IMAGE1_SRAM_PRIORITY_REG (SRAM_MASTER_CFG_REG + 0x24)
#define SCALE1_SRAM_PRIORITY_REG (SRAM_MASTER_CFG_REG + 0x28)

extern void *reserved_mem;
extern int cedarmem_size;

struct iomap_para{
	volatile char* regs_macc;
	volatile char* regs_ccmu;
};

static DECLARE_WAIT_QUEUE_HEAD(wait_ve);

struct cedar_dev {
	struct cdev cdev;	             /* char device struct                 */
	struct device *dev;              /* ptr to class device struct         */
	struct class  *class;            /* class for auto create device node  */

	struct semaphore sem;            /* mutual exclusion semaphore         */
	spinlock_t  lock;                /* spinlock to pretect ioctl access   */

	wait_queue_head_t wq;            /* wait queue for poll ops            */

	struct iomap_para iomap_addrs;   /* io remap addrs                     */

	u32 irq;                         /* cedar video engine irq number      */
	u32 irq_flag;                    /* flag of video engine irq generated */
	u32 irq_value;                   /* value of video engine irq          */
	u32 irq_has_enable;
};
struct cedar_dev *cedar_devp;

u32 int_sta=0,int_value;

/*
 * Video engine interrupt service routine
 * To wake up ve wait queue
 */
static irqreturn_t VideoEngineInterupt(int irq, void *dev)
{
    volatile int* ve_int_ctrl_reg;
    //volatile int* ve_status_reg;
    volatile int* modual_sel_reg;
    int modual_sel;

	struct iomap_para addrs = cedar_devp->iomap_addrs;

    modual_sel_reg = (int *)(addrs.regs_macc + 0);

    modual_sel = *modual_sel_reg;
    modual_sel &= 0xf;

//printk("kernel-interrupt!\n");

	/* estimate Which video format */
    switch (modual_sel)
    {
        case 0: //mpeg124
            //ve_status_reg = (int *)(addrs.regs_macc + 0x100 + 0x1c);
            ve_int_ctrl_reg = (int *)(addrs.regs_macc + 0x100 + 0x14);
            break;
        case 1: //h264
            //ve_status_reg = (int *)(addrs.regs_macc + 0x200 + 0x28);
            ve_int_ctrl_reg = (int *)(addrs.regs_macc + 0x200 + 0x20);
            break;
        case 2: //vc1
            //ve_status_reg = (int *)(addrs.regs_macc + 0x300 + 0x2c);
            ve_int_ctrl_reg = (int *)(addrs.regs_macc + 0x300 + 0x24);
            break;
        case 3: //rmvb
            //ve_status_reg = (int *)(addrs.regs_macc + 0x400 + 0x1c);
            ve_int_ctrl_reg = (int *)(addrs.regs_macc + 0x400 + 0x14);
            break;
        default:
            //ve_status_reg = (int *)(addrs.regs_macc + 0x100 + 0x1c);
            ve_int_ctrl_reg = (int *)(addrs.regs_macc + 0x100 + 0x14);
            printk("macc modual sel not defined!\n");
            break;
    }

    //disable interrupt
    if(modual_sel == 0)
        *ve_int_ctrl_reg = *ve_int_ctrl_reg & (~0x7C);
    else
        *ve_int_ctrl_reg = *ve_int_ctrl_reg & (~0xF);

	cedar_devp->irq_value = 0;//*ve_status_reg;
	//*ve_status_reg = cedar_devp->irq_value;
	cedar_devp->irq_flag = 1;

	//printk("ve status = %#x\n", cedar_devp->irq_value);
    //any interrupt will wake up wait queue
    wake_up_interruptible(&wait_ve);        //ioctl

	// wait up poll wait queue
	// cedar_devp->irq_flag = 1;
	// wake_up_interruptible(&cedar_devp->wq); //poll
    return IRQ_HANDLED;
}

/*
 * poll operateion for wait for ve irq
 */
unsigned int cedardev_poll(struct file *filp, struct poll_table_struct *wait)
{
	int mask = 0;
	struct cedar_dev *devp = filp->private_data;

	poll_wait(filp, &devp->wq, wait);

	if (devp->irq_flag == 1) {
		devp->irq_flag = 0;
		mask |= POLLIN | POLLRDNORM;
	}

	return mask;
}


//#define dbg_print printk

/*
 * ioctl function
 * including : wait video engine done,
 *             AVS Counter control,
 *             Physical memory control,
 *             module clock/freq control.
 */
long cedardev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    long   ret = 0;
    unsigned int v;
	spinlock_t *lock;
	struct cedar_dev *devp;

	ret = 0;
	devp = filp->private_data;
	lock = &devp->lock;

    //printk("cedardev_ioctl_cmd: -- 0x%x --\n",cmd);

    switch (cmd)
    {
        case IOCTL_WAIT_VE:
            wait_event_interruptible(wait_ve, cedar_devp->irq_flag);
	        cedar_devp->irq_flag = 0;
			return cedar_devp->irq_value;

		case IOCTL_ENABLE_VE:
            v = readl(CCMU_VE_CLK_REG);
			v |= (0x1 << 7);              // Do not gate the speccial clock for DE
			writel(v, CCMU_VE_CLK_REG);
			break;

		case IOCTL_DISABLE_VE:
            v = readl(CCMU_VE_CLK_REG);
			v &= ~(0x1 << 7);             // Gate the speccial clock for DE
			writel(v, CCMU_VE_CLK_REG);
			break;

		case IOCTL_RESET_VE:
			v = readl(CCMU_SDRAM_PLL_REG);
			v &= ~(0x1 << 24);
			writel(v, CCMU_SDRAM_PLL_REG);
			//----------------------------
			v = readl(CCMU_VE_CLK_REG);
			v &= ~(0x1 << 5);             // Reset VE
			writel(v, CCMU_VE_CLK_REG);
			v |= (0x1 << 5);              // Reset VE
			writel(v, CCMU_VE_CLK_REG);
			//-----------------------------
			v = readl(CCMU_SDRAM_PLL_REG);
			v |= (0x1 << 24);
			writel(v, CCMU_SDRAM_PLL_REG);
			break;

		case IOCTL_SET_VE_FREQ:
			/* VE Clock Setting */
			v = readl(CCMU_VE_PLL_REG);
			v |= (0x1 << 15);             // Enable VE PLL;
			v &= ~(0x1 << 12);            // Disable ByPass;
			v |= (arg - 30) / 6;
			writel(v, CCMU_VE_PLL_REG);
			break;

        case IOCTL_GETVALUE_AVS2:
			/* Return AVS1 counter value */
            return readl(devp->iomap_addrs.regs_ccmu + 0xc88);

        case IOCTL_ADJUST_AVS2:
        {
            int arg_s = (int)arg;
            int temp;
            v = readl(devp->iomap_addrs.regs_ccmu + 0xc44);
            temp = v & 0xffff0000;
            temp =temp + temp*arg_s/100;
            v = (temp & 0xffff0000) | (v&0x0000ffff);
            writel(v, devp->iomap_addrs.regs_ccmu + 0xc44);
            break;
        }

        case IOCTL_CONFIG_AVS2:
			/* Set AVS counter divisor */
            v = readl(devp->iomap_addrs.regs_ccmu + 0xc8c);
            v = 239 << 16 | (v & 0xffff);
            writel(v, devp->iomap_addrs.regs_ccmu + 0xc8c);

			/* Enable AVS_CNT1 and Pause it */
            v = readl(devp->iomap_addrs.regs_ccmu + 0xc80);
            v |= 1 << 9 | 1 << 1;
            writel(v, devp->iomap_addrs.regs_ccmu + 0xc80);

			/* Set AVS_CNT1 init value as zero  */
            writel(0, devp->iomap_addrs.regs_ccmu + 0xc88);

#ifdef CHIP_VERSION_F20
            v = readl(devp->iomap_addrs.regs_ccmu + 0x040);
            v |= 1<<8;
            writel(v, devp->iomap_addrs.regs_ccmu + 0x040);
#endif
            break;

        case IOCTL_RESET_AVS2:
            /* Set AVS_CNT1 init value as zero */
            writel(0, devp->iomap_addrs.regs_ccmu + 0xc88);
            break;

        case IOCTL_PAUSE_AVS2:
            /* Pause AVS_CNT1 */
            v = readl(devp->iomap_addrs.regs_ccmu + 0xc80);
            v |= 1 << 9;
            writel(v, devp->iomap_addrs.regs_ccmu + 0xc80);
            break;

        case IOCTL_START_AVS2:
		    /* Start AVS_CNT1 : do not pause */
            v = readl(devp->iomap_addrs.regs_ccmu + 0xc80);
            v &= ~(1 << 9);
            writel(v, devp->iomap_addrs.regs_ccmu + 0xc80);
            break;

        case IOCTL_GET_ENV_INFO:
        {
            cedarv_env_info_t env_info;

            env_info.phymem_start = (unsigned int)reserved_mem;
	        env_info.address_macc = (unsigned int)cedar_devp->iomap_addrs.regs_macc;
            env_info.phymem_total_size = cedarmem_size;
            if (copy_to_user((char *)arg, &env_info, sizeof(cedarv_env_info_t)))
                return -EFAULT;

            break;
        }
/*
        case IOCTL_ENABLE_IRQ:
            if(devp->irq_has_enable == 0){
                int ret = request_irq(VE_IRQ_NO, VideoEngineInterupt, 0, "cedar_dev", NULL);
            	if (ret < 0) {
            		printk("request irq err\n");
            		return -EINVAL;
            	}
                //enable_irq(cedar_devp->irq);
                devp->irq_has_enable = 1;
            }
            break;

        case IOCTL_DISABLE_IRQ:
            if(devp->irq_has_enable){
                //disable_irq(cedar_devp->irq);
                free_irq(VE_IRQ_NO, NULL);
                devp->irq_has_enable = 0;
            }
            break;
*/
        default:
            break;
    }

    return ret;
}

static int cedardev_open(struct inode *inode, struct file *filp)
{
	struct cedar_dev *devp;
	devp = container_of(inode->i_cdev, struct cedar_dev, cdev);
	filp->private_data = devp;

	if (down_interruptible(&devp->sem)) {
		return -ERESTARTSYS;
	}

	/* init other resource here */
    devp->irq_flag = 0;

	up(&devp->sem);

	nonseekable_open(inode, filp);
	return 0;
}

static int cedardev_release(struct inode *inode, struct file *filp)
{
	struct cedar_dev *devp;

	devp = filp->private_data;

	if (down_interruptible(&devp->sem)) {
		return -ERESTARTSYS;
	}

	/* release other resource here */
    devp->irq_flag = 1;

	up(&devp->sem);
	return 0;
}

void cedardev_vma_open(struct vm_area_struct *vma)
{
//    printk(KERN_NOTICE "cedardev VMA open, virt %lx, phys %lx\n",
//		vma->vm_start, vma->vm_pgoff << PAGE_SHIFT);
	return;
}

void cedardev_vma_close(struct vm_area_struct *vma)
{
//    printk(KERN_NOTICE "cedardev VMA close.\n");
	return;
}

static struct vm_operations_struct cedardev_remap_vm_ops = {
    .open  = cedardev_vma_open,
    .close = cedardev_vma_close,
};

static int cedardev_mmap(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long temp_pfn;
    unsigned int  VAddr;
	struct iomap_para addrs;

	unsigned int io_ram = 0;
    VAddr = vma->vm_pgoff << 12;

	addrs = cedar_devp->iomap_addrs;

    if(VAddr == (unsigned int)addrs.regs_macc) {
        temp_pfn = MACC_REGS_BASE >> 12;
        io_ram = 1;
    } else {
        temp_pfn = (__pa(vma->vm_pgoff << 12))>>12;
        io_ram = 0;
    }

    if (io_ram == 0) {
        /* Set reserved and I/O flag for the area. */
        vma->vm_flags |= VM_RESERVED | VM_IO;

        /* Select uncached access. */
        vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

        if (remap_pfn_range(vma, vma->vm_start, temp_pfn,
                            vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
            return -EAGAIN;
        }
    } else {
        /* Set reserved and I/O flag for the area. */
        vma->vm_flags |= VM_RESERVED | VM_IO;
        /* Select uncached access. */
        vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

        if (io_remap_pfn_range(vma, vma->vm_start, temp_pfn,
                               vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
            return -EAGAIN;
        }
    }

    vma->vm_ops = &cedardev_remap_vm_ops;
    cedardev_vma_open(vma);

    return 0;
}

static struct file_operations cedardev_fops = {
    .owner   = THIS_MODULE,
    .mmap    = cedardev_mmap,
	.poll    = cedardev_poll,
    .open    = cedardev_open,
    .release = cedardev_release,
	.llseek  = no_llseek,
    .unlocked_ioctl   = cedardev_ioctl,
};



static int __init cedardev_init(void)
{
	int ret;
	int devno;
	unsigned int val;
	dev_t dev = 0;

	if (g_dev_major) {
		dev = MKDEV(g_dev_major, g_dev_minor);
		ret = register_chrdev_region(dev, 1, "cedar_dev");
	} else {
		ret = alloc_chrdev_region(&dev, g_dev_minor, 1, "cedar_dev");
		g_dev_major = MAJOR(dev);
		g_dev_minor = MINOR(dev);
	}

	if (ret < 0) {
		printk(KERN_WARNING "cedar_dev: can't get major %d\n", g_dev_major);
		return ret;
	}

	cedar_devp = kmalloc(sizeof(struct cedar_dev), GFP_KERNEL);
	if (cedar_devp == NULL) {
		printk("malloc mem for cedar device err\n");
		return -ENOMEM;
	}
	memset(cedar_devp, 0, sizeof(struct cedar_dev));
	cedar_devp->irq = VE_IRQ_NO;

	init_MUTEX(&cedar_devp->sem);
	init_waitqueue_head(&cedar_devp->wq);

	memset(&cedar_devp->iomap_addrs, 0, sizeof(struct iomap_para));

    ret = request_irq(VE_IRQ_NO, VideoEngineInterupt, 0, "cedar_dev", NULL);
    if (ret < 0) {
        printk("request irq err\n");
        return -EINVAL;
    }

    /* map for macc io space */
    cedar_devp->iomap_addrs.regs_macc = ioremap(MACC_REGS_BASE, 4096);
    if (!cedar_devp->iomap_addrs.regs_macc){
        printk("cannot map region for macc");
    }

     cedar_devp->iomap_addrs.regs_ccmu = ioremap(CCMU_REGS_BASE, 4096);
     if (!cedar_devp->iomap_addrs.regs_ccmu){
        printk("cannot map region for ccmu");
     }

	/* DRAM Master Priority */
//#if 0
//	// set cpu priority
//	val = readl(CPU_SRAM_PRIORITY_REG);
//	printk("cpu -> %#x\n", val);
//	val &= ~(0x3 << 2);
//	val |= (0x3 << 2);
//	//val &= ~(0xFF << 8);
//	//val |= (0xF << 8);
//	writel(val, CPU_SRAM_PRIORITY_REG);
//#endif
//#if 0 //-----------------------------------------------------------
//	// set de priority
//	val = readl(VE_SRAM_PRIORITY_REG);
//	printk("ve -> %#x\n", val);
//	val &= ~(0x3 << 2);
//	val |= (0x3 << 2);
//	//val &= ~(0xFF << 8);
//	//val |= (0xF << 8);
//	writel(val, VE_SRAM_PRIORITY_REG);
//
//	// set image0 priority
//	val = readl(IMAGE0_SRAM_PRIORITY_REG);
//	printk("image0 -> %#x\n", val);
//	val &= ~(0x3 << 2);
//	val |= (0x3 << 2);
//	//val &= ~(0xFF << 8);
//	//val |= (0x7 << 8);
//	writel(val, IMAGE0_SRAM_PRIORITY_REG);
//
//	// set scale0 priority
//	val = readl(SCALE0_SRAM_PRIORITY_REG);
//	printk("scale0 -> %#x\n", val);
//	val &= ~(0x3 << 2);
//	val |= (0x3 << 2);
//	//val &= ~(0xFF << 8);
//	//val |= (0x7 << 8);
//	writel(val, SCALE0_SRAM_PRIORITY_REG);
//
//	// set image1 priority
//	val = readl(IMAGE1_SRAM_PRIORITY_REG);
//	printk("image1 -> %#x\n", val);
//	val &= ~(0x3 << 2);
//	val |= (0x3 << 2);
//	//val &= ~(0xFF << 8);
//	//val |= (0x7 << 8);
//	writel(val, IMAGE1_SRAM_PRIORITY_REG);
//
//	// set scale1 priority
//	val = readl(SCALE1_SRAM_PRIORITY_REG);
//	printk("scale1 -> %#x\n", val);
//	val &= ~(0x3 << 2);
//	val |= (0x3 << 2);
//	//val &= ~(0xFF << 8);
//	//val |= (0x7 << 8);
//	writel(val, SCALE1_SRAM_PRIORITY_REG);
//	printk("set cpu image0/1 scale0/1 ve dram priority\n");
//#endif




//F20 provide by zyf
//0x01c01060: 0x0000_0309  //for CPU-D
//0x01c01064: 0x0000_0309  //for CPU-I
//0x01c01068: 0x0000_0301  //for DMA
//0x01c0106c: 0x0000_0305  //for GPS
//0x01c01070: 0x0003_0701  //for BE0
//0x01c01074: 0x0003_1001  //for FE0
//0x01c01078: 0x0003_1001  //for CSI0
//0x01c0107c: 0x0003_0701  //for TS
//0x01c01080: 0x0003_1001  //for VE
//0x01c01084: 0x0003_0701  //for BE1
//0x01c01088: 0x0003_1001  //for FE1
//0x01c0108c: 0x0003_0701  //for MP
//0x01c01090: 0x0000_0000  //reserved
//0x01c01094: 0x0003_1001  //for CSI1
//0x01c01098: 0x0003_1001  //for AECE
//0x01c0109c: 0x0000_0301  //for N-DMA/SDC

	writel(0x00000309, 0xf1c01060);
	writel(0x00000309, 0xf1c01064);
	writel(0x00000301, 0xf1c01068);
	writel(0x00000305, 0xf1c0106c);
	writel(0x00030701, 0xf1c01070);
	writel(0x00031001, 0xf1c01074);
	writel(0x00031001, 0xf1c01078);
	writel(0x00030701, 0xf1c0107c);
	writel(0x00031001, 0xf1c01080);
	writel(0x00030701, 0xf1c01084);
	writel(0x00031001, 0xf1c01088);
	writel(0x00030701, 0xf1c0108c);
	writel(0x00000000, 0xf1c01090);
	writel(0x00031001, 0xf1c01094);
	writel(0x00031001, 0xf1c01098);
	writel(0x00000301, 0xf1c0109c);


	/* VE Clock Setting */
	val = readl(CCMU_VE_PLL_REG);
	val |= (0x1 << 15);             // Enable VE PLL;
	val &= ~(0x1 << 12);            // Disable ByPass;
	//VE freq: 0x16->162; 0x17->168; 0x18->174; 0x19->180
	//         0x1a->186; 0x1b->192; 0x1c->198; 0x1d->204
	//         0x1e->210;
	val |= 0x19;
	writel(val, CCMU_VE_PLL_REG);
	printk("-----[cedar_dev] set ve clock -> %#x\n", val);

	val = readl(CCMU_AHB_GATE_REG);
	val |= (0x1 << 15);             // Disable VE AHB clock gating
	writel(val, CCMU_AHB_GATE_REG);

	val = readl(CCMU_VE_CLK_REG);
	val |= (0x1 << 7);              // Do not gate the speccial clock for DE
	val |= (0x1 << 5);              // Reset VE
	writel(val, CCMU_VE_CLK_REG);

	val = readl(CCMU_SDRAM_PLL_REG);
	val |= (0x1 << 24);             // Do not gate DRAM clock for VD
	writel(val, CCMU_SDRAM_PLL_REG);

	/* Security System  clock setting */
	val = readl(CCMU_TS_SS_CLK_REG);
	val |= (0x1 << 15);             // Do not gate the special clock for SS
	val &= ~(0x3 << 12);            // Chosse video pll0
	val &= ~(0xF << 8);             // Set SS freq as 1/2 video pll clock
	val |= (0x1 << 8);
	writel(val, CCMU_TS_SS_CLK_REG);

	/* Create char device */
	devno = MKDEV(g_dev_major, g_dev_minor);
	cdev_init(&cedar_devp->cdev, &cedardev_fops);
	cedar_devp->cdev.owner = THIS_MODULE;
	cedar_devp->cdev.ops = &cedardev_fops;
	ret = cdev_add(&cedar_devp->cdev, devno, 1);
	if (ret) {
		printk(KERN_NOTICE "Err:%d add cedardev", ret);
	}

    cedar_devp->class = class_create(THIS_MODULE, "cedar_dev");
    cedar_devp->dev   = device_create(cedar_devp->class, NULL, devno, NULL, "cedar_dev");

	return 0;
}
module_init(cedardev_init);

static void __exit cedardev_exit(void)
{
	dev_t dev;
	dev = MKDEV(g_dev_major, g_dev_minor);

    free_irq(VE_IRQ_NO, NULL);
	iounmap(cedar_devp->iomap_addrs.regs_macc);
	iounmap(cedar_devp->iomap_addrs.regs_ccmu);

	/* Destroy char device */
	if (cedar_devp) {
		cdev_del(&cedar_devp->cdev);
		device_destroy(cedar_devp->class, dev);
		class_destroy(cedar_devp->class);
	}

	unregister_chrdev_region(dev, 1);

	if (cedar_devp) {
		kfree(cedar_devp);
	}
}
module_exit(cedardev_exit);

MODULE_AUTHOR("Soft-Allwinner");
MODULE_DESCRIPTION("User mode CEDAR device interface");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

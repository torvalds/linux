#ifndef __RKCAMSYS_INTERNAL_H__
#define __RKCAMSYS_INTERNAL_H__

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>	
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/fs.h>	
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/types.h>	
#include <linux/proc_fs.h>
#include <linux/fcntl.h>	
#include <linux/clk.h>
#include <linux/seq_file.h>
#include <linux/cdev.h>
#include <linux/miscdevice.h> 
#include <linux/version.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/regulator/machine.h>
#include <linux/log2.h>
//#include <mach/io.h>
//#include <mach/gpio.h>
//#include <mach/iomux.h>
//#include <mach/cru.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/iomap.h>
#include <linux/rockchip/grf.h>

#include <asm/gpio.h>
#include <asm/system.h>	
#include <asm/uaccess.h>

#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/rockchip/cpu.h>
#include <media/camsys_head.h>



/*
*               C A M S Y S   D R I V E R   V E R S I O N 
*
*v0.0.1:
*        1) test version;
*v0.0.2:
*        1) add mipi csi phy;
*v0.0.3:
*        1) add support cif phy for marvin;
*v0.0.4:
*        1) add clock information in struct camsys_devio_name_s;
*v0.0.5:
*        1) set isp clock at 32MHz;
*v0.0.6:
*        1) iomux d0 d1 for cif phy raw10 in rk319x after i2c operated;
*        2) check mis value in camsys_irq_connect;
*        3) add soft rest callback;
*v0.7.0:
*        1) check extdev is activate or not before delete from camsys_dev active list;
*v0.8.0:
*        1) fix deregister a unregister extdev oops in camsys_extdev_deregister;
*v0.9.0: 1) set isp freq to 210M
*v0.a.0: 
*        1) fix camsys_i2c_write and camsys_i2c_write can't support reg_size=0;
*v0.b.0:
		 1) control ddr freq by marvin self other than by clk unit. 
*v0.c.0:
*        1) add flash_trigger_out control
*v0.d.0:
*        1) add Isp_SoftRst for rk3288;
*v0.e.0:
*        1) isp_clk 208.8M for 1lane, isp_clk 416.6M for 2lane;
*v0.f.0:
		 1) mi_mis register may read erro, this may cause mistaken mi frame_end irqs.  
*/
#define CAMSYS_DRIVER_VERSION                   KERNEL_VERSION(0,0xe,0)


#define CAMSYS_PLATFORM_DRV_NAME                "RockChip-CamSys"
#define CAMSYS_PLATFORM_MARVIN_NAME             "Platform_MarvinDev"
#define CAMSYS_PLATFORM_CIF0_NAME               "Platform_Cif0Dev"
#define CAMSYS_PLATFORM_CIF1_NAME               "Platform_Cif1Dev"

#define CAMSYS_REGISTER_RES_NAME                "CamSys_RegMem"
#define CAMSYS_REGISTER_MIPIPHY_RES_NAME        "CamSys_RegMem_MipiPhy"
#define CAMSYS_IRQ_RES_NAME                     "CamSys_Irq"

#define CAMSYS_REGISTER_MEM_NAME                CAMSYS_REGISTER_RES_NAME
#define CAMSYS_I2C_MEM_NAME                     "CamSys_I2cMem"
#define CAMSYS_MIPIPHY_MEM_NAME                 CAMSYS_REGISTER_MIPIPHY_RES_NAME

#define CAMSYS_NAMELEN_MIN(a)                   ((strlen(a)>(CAMSYS_NAME_LEN-1))?(CAMSYS_NAME_LEN-1):strlen(a))
#define CAMSYS_IRQPOOL_NUM                      128

extern unsigned int camsys_debug;

#define camsys_trace(level, msg,...) \
	do { \
		if (camsys_debug >= level) \
			printk("D%d:%s(%d): " msg "\n",level, __FUNCTION__,__LINE__, ## __VA_ARGS__); \
	} while (0)

#define camsys_warn(msg,...)  printk(KERN_ERR "W:%s(%d): " msg "\n", __FUNCTION__,__LINE__, ## __VA_ARGS__)
#define camsys_err(msg,...)   printk(KERN_ERR "E:%s(%d): " msg "\n", __FUNCTION__,__LINE__, ## __VA_ARGS__)


typedef struct camsys_irqstas_s {
    camsys_irqsta_t       sta;
    struct list_head      list;
} camsys_irqstas_t;

typedef struct camsys_irqpool_s {
    pid_t                 pid;
    unsigned int          timeout;             //us
    unsigned int          mis;
    unsigned int          icr;
    

    spinlock_t            lock;                       // lock for list
    camsys_irqstas_t      pool[CAMSYS_IRQPOOL_NUM];
    struct list_head      active;
    struct list_head      deactive;

    struct list_head      list;

    wait_queue_head_t     done;
} camsys_irqpool_t;

typedef struct camsys_irq_s {
    unsigned int          irq_id;

    spinlock_t            lock;             //lock for timeout and irq_connect in ioctl
    //unsigned int          timeout;
    
    //wait_queue_head_t     irq_done;

    struct list_head      irq_pool;
} camsys_irq_t;

typedef struct camsys_meminfo_s {
    unsigned char name[32];
    unsigned int phy_base;
    unsigned int vir_base;
    unsigned int size;
    unsigned int vmas;

    struct list_head list;
    
} camsys_meminfo_t;

typedef struct camsys_devmems_s {
    camsys_meminfo_t *registermem;
    camsys_meminfo_t *i2cmem;
    struct list_head memslist;
} camsys_devmems_t;

typedef struct camsys_regulator_s {
    struct regulator  *ldo;
    int               min_uv;
    int               max_uv;
} camsys_regulator_t;

typedef struct camsys_gpio_s {
    unsigned int      io;
    unsigned int      active;
} camsys_gpio_t;
typedef struct camsys_flash_s {
    camsys_gpio_t        fl;
} camsys_flash_t;
typedef struct camsys_extdev_s {
    unsigned int             dev_id;
    camsys_regulator_t       avdd;
    camsys_regulator_t       dovdd;
    camsys_regulator_t       dvdd;
    camsys_regulator_t       afvdd;
    
    camsys_gpio_t            pwrdn;
    camsys_gpio_t            rst;
    camsys_gpio_t            afpwr;
    camsys_gpio_t            afpwrdn;
	camsys_gpio_t            pwren;

    camsys_flash_t           fl;

    camsys_extdev_phy_t      phy;
    camsys_extdev_clk_t      clk;
    
    unsigned int             dev_cfg;

    struct platform_device *pdev;
    
    struct list_head         list;
    struct list_head         active;
} camsys_extdev_t;

typedef struct camsys_phyinfo_s {
    unsigned int             phycnt;
    void                     *clk;
    camsys_meminfo_t         *reg;    

    int (*clkin_cb)(void *ptr, unsigned int on);
    int (*ops) (void *ptr, camsys_mipiphy_t *phy);
    int (*remove)(struct platform_device *pdev);
} camsys_phyinfo_t;

typedef struct camsys_exdevs_s {
    struct mutex          mut;
    struct list_head      list;
    struct list_head      active;
} camsys_exdevs_t;

typedef struct camsys_dev_s {
    unsigned int          dev_id;	  

    camsys_irq_t          irq;
    camsys_devmems_t      devmems;
    struct miscdevice     miscdev;  
    void                  *clk;

    camsys_phyinfo_t      *mipiphy;
    camsys_phyinfo_t      cifphy;

    camsys_exdevs_t       extdevs;    
    struct list_head      list;
    struct platform_device *pdev;

    void                  *soc;

    int (*clkin_cb)(void *ptr, unsigned int on);
    int (*clkout_cb)(void *ptr,unsigned int on,unsigned int clk);
    int (*reset_cb)(void *ptr, unsigned int on);
    int (*phy_cb) (camsys_extdev_t *extdev, camsys_sysctrl_t *devctl, void* ptr);
    int (*iomux)(camsys_extdev_t *extdev,void *ptr);
    int (*platform_remove)(struct platform_device *pdev);
    int (*flash_trigger_cb)(void *ptr, unsigned int on);
} camsys_dev_t;


static inline camsys_extdev_t* camsys_find_extdev(unsigned int dev_id, camsys_dev_t *camsys_dev)
{
    camsys_extdev_t *extdev;

    if (!list_empty(&camsys_dev->extdevs.list)) {
        list_for_each_entry(extdev, &camsys_dev->extdevs.list, list) {
            if (extdev->dev_id == dev_id) {
                return extdev;
            }
        }
    }    
    return NULL;
}

static inline camsys_meminfo_t* camsys_find_devmem(char *name, camsys_dev_t *camsys_dev)
{
    camsys_meminfo_t *devmem;

    if (!list_empty(&camsys_dev->devmems.memslist)) {
        list_for_each_entry(devmem, &camsys_dev->devmems.memslist, list) {
            if (strcmp(devmem->name, name) == 0) {
                return devmem;
            }
        }
    }
    camsys_err("%s memory have not been find in %s!",name,dev_name(camsys_dev->miscdev.this_device));
    return NULL;
}


static inline int camsys_sysctl_extdev(camsys_extdev_t *extdev, camsys_sysctrl_t *devctl, camsys_dev_t *camsys_dev)
{
    int err = 0;
    camsys_regulator_t *regulator;
    camsys_gpio_t *gpio;
    
    if ((devctl->ops>CamSys_Vdd_Start_Tag) && (devctl->ops < CamSys_Vdd_End_Tag)) {
        regulator = &extdev->avdd;
        regulator += devctl->ops-1;
        
        if (!IS_ERR_OR_NULL(regulator->ldo)) {
            if (devctl->on) {
                err = regulator_set_voltage(regulator->ldo,regulator->min_uv,regulator->max_uv);
                err |= regulator_enable(regulator->ldo);
                camsys_trace(1,"Sysctl %d success, regulator set (%d,%d) uv!",devctl->ops, regulator->min_uv,regulator->max_uv);
            } else {
                while(regulator_is_enabled(regulator->ldo)>0)	
			        regulator_disable(regulator->ldo);
			    camsys_trace(1,"Sysctl %d success, regulator off!",devctl->ops);
            }
        } else {
            //camsys_err("Sysctl %d failed, because regulator ldo is NULL!",devctl->ops);
            err = -EINVAL;
            goto end;
        }
    } else if ((devctl->ops>CamSys_Gpio_Start_Tag) && (devctl->ops < CamSys_Gpio_End_Tag)) {
        gpio = &extdev->pwrdn;
        gpio += devctl->ops - CamSys_Gpio_Start_Tag -1;

        if (gpio->io != 0xffffffff) {
            if (devctl->on) {
                gpio_direction_output(gpio->io, gpio->active);
                gpio_set_value(gpio->io, gpio->active);
                camsys_trace(1,"Sysctl %d success, gpio(%d) set %d",devctl->ops, gpio->io, gpio->active);
            } else {
                gpio_direction_output(gpio->io, !gpio->active);
                gpio_set_value(gpio->io, !gpio->active);
                camsys_trace(1,"Sysctl %d success, gpio(%d) set %d",devctl->ops, gpio->io, !gpio->active);
            }
        } else {
            camsys_err("Sysctl %d failed, because gpio is NULL!",devctl->ops);
            err = -EINVAL;
            goto end;
        }
    } else if (devctl->ops == CamSys_ClkIn) {
        if (camsys_dev->clkout_cb)
            camsys_dev->clkout_cb(camsys_dev,devctl->on,extdev->clk.in_rate);        
    } else if (devctl->ops == CamSys_Phy) {
        if (camsys_dev->phy_cb)
            (camsys_dev->phy_cb)(extdev,devctl,(void*)camsys_dev);
    }

end:
    return err;
}

extern struct file_operations camsys_fops;
#endif

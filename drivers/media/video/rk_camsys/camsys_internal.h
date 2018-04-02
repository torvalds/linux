/* SPDX-License-Identifier: GPL-2.0 */
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
#include <linux/gpio.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/grf.h>
#include <asm/uaccess.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/rockchip/cpu.h>
#include <media/camsys_head.h>
#include <linux/rockchip-iovmm.h>

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
		3) add soft rest callback;
*v0.7.0:
		1) check extdev is activate or not before delete from
		camsys_dev active list;
*v0.8.0:
		1) fix deregister a unregister extdev oops
		in camsys_extdev_deregister;
*v0.9.0: 1) set isp freq to 210M
*v0.a.0: 
		1) fix camsys_i2c_write and camsys_i2c_write
		can't support reg_size=0;
*v0.b.0:
		1) control ddr freq by marvin self other than by clk unit.
*v0.c.0:
*        1) add flash_trigger_out control
*v0.d.0:
*        1) add Isp_SoftRst for rk3288;
*v0.e.0:
*        1) isp_clk 208.8M for 1lane, isp_clk 416.6M for 2lane;
*v0.f.0:
		1) mi_mis register may read erro, this may cause
		mistaken mi frame_end irqs.
*v0.0x10.0:
		1) add flash_prelight control.
*v0.0x11.0:
		1) raise qos of isp up to the same as lcdc.
*v0.0x12.0:
		1) support iommu.
*v0.0x13.0:
		1) camsys_extdev_register return failed when this
		dev_id has been registered;
		2) add support JPG irq connect;
*v0.0x14.0:
		1) camsys_extdev_register return -EBUSY when this
		dev_id has been registered;
*v0.0x15.0:
		1) check extdev name when dev_id has been registered;
*v0.0x16.0:
		1) enable or disable IOMMU just depending
		on CONFIG_ROCKCHIP_IOMMU.
*v0.0x17.0:
		1) isp iommu status depend on vpu iommu status.
*v0.0x18.0:
		1) add flashlight RT8547 driver
		2) support torch mode
*v0.0x19.0:
		1) set CONFIG_CAMSYS_DRV disable as default,
		enable in defconfig file if needed.
*v0.0x1a.0:
		1) vpu_node changed from "vpu_service" to "rockchip,vpu_sub"
*v0.0x1b.0:
		1) use of_find_node_by_name to get vpu node
		instead of of_find_compatible_node
*v0.0x1c.0:
		1) support rk3368.
*v0.0x1d.0:
		1) enable aclk_rga for rk3368, otherwise,
		isp reset will cause system halted.
*v0.0x1e.0:
		1) dts remove aclk_rga, change aclk_isp
		from <clk_gates17 0> to <&clk_gates16 0>.
		2) add rl3369 pd_isp enable/disable.
*v0.0x1f.0:
		1) GPIO(gpio7 GPIO_B5) is EBUSY
		when register after factory reset,
		but after power on ,it's normal.
*v0.0x20.0:
		1) rk3368 camera: hold vio0 noc clock during the camera work,
		fixed isp iommu stall failed.
*v0.0x21.0:
		1) add isp-dvp-d4d11 iomux support.
*v0.0x21.1:
		1) support rk3368-sheep kernel ver4.4.
*v0.0x21.2:
		1) support rk3399.
*v0.0x21.3:
		1) some modifications.
*v0.0x21.4:
		1) modify for rk3399.
*v0.0x21.5:
		1) modify for mipiphy hsfreqrange.
*v0.0x21.6:
		1) support drm iommu.
*v0.0x21.7:
*       1) remove memset function wrong called code.
*v0.0x21.8:
*       1) flash module exist risk, fix up it.
*v0.0x21.9:
	1) fix drm iommu crash.
	if process cameraserver was died during streaming, iommu resource
	was not released correctly. when cameraserver was recovered and
	streaming again, iommu resource may be conflicted.
*v0.0x21.0xa:
	1) clock clk_vio0_noc would cause mipi lcdc no display on 3368h, remove it.
*v0.0x21.0xb:
	1) some log is boring, so set print level more high.
*v0.0x21.0xc:
	1) support rk3288.
*v0.0x21.0xd:
	1) modify mipiphy_hsfreqrange for 3368.
*v0.0x21.0xe
	1) correct mipiphy_hsfreqrange of 3368.
	2) add csi-phy timing setting for 3368.
*v0.0x21.0xf:
	1) add reference count for marvin.
*v0.0x22.0:
	1) delete node in irqpool list when thread disconnect.
*v0.0x22.1:
	1) gpio0_D is unavailable on rk3288 with current pinctrl driver.
*v0.0x22.2:
	1) modify the condition of DRM iommu, which makes code  more readable
	by using of_parse_phandle to check whether the "iommus" phandle exists
	in the isp device node.
*v0.0x22.3:
	1) switch TX1/RX1 D-PHY of rk3288/3399 to RX status before
	it's initialization to avoid conflicting with sensor output.
*v0.0x22.4:
	1) enable SYS_STATUS_ISP status set.
*v0.0x22.5:
	1) gpio base start from 1000,adapt to it.
*v0.0x22.6:
	1) revert v0.0x22.3.
*v0.0x22.7:
	1) read MRV_MIPI_FRAME register in camsys_mrv_irq, and pass the value
	fs_id and fe_id into isp library.
*v0.0x22.8:
	1) 3399 power management is wrong, correct it.
*v0.0x23.0:
       1) replace current->pid with irqsta->pid.
*v0.0x24.0:
       1) function is the same as commit in v0.0x22.3 but now is better way.
*v0.0x25.0:
	1) support px30.
*v0.0x26.0:
       1) v0.0x21.9 may not fix all the case of iommu issue caused by the
       unexpected termination of process cameraserver, so we force to release
       all iommu resource in |.release| of fops aganin if needed.
*v0.0x27.0:
       1) revert v0.0x22.5.
*v0.0x28.0:
       1) fix isp soft reset failure for rk3326.
       reset on too high aclk rate will result in bus dead, so we reduce the aclk
       before reset and then recover it after reset.
*v0.0x28.1:
       1) another reasonable solution of isp soft reset failure for rk3326.
       reset on too high isp_clk rate will result in bus dead.
       The signoff isp_clk rate is 350M, and the recommended rate
       on reset from IC is NOT greater than 300M.
*v0.0x29.0:
       1) fix camera mipi phy config for rk3288.
	   CSIHOST_PHY_SHUTDOWNZ and CSIHOST_DPHY_RSTZ is
	   csi host control interface;so DPHY_RX1_SRC_SEL_MASK
	   should be set DPHY_RX1_SRC_SEL_CSI.
*/

#define CAMSYS_DRIVER_VERSION                   KERNEL_VERSION(0, 0x29, 0)

#define CAMSYS_PLATFORM_DRV_NAME                "RockChip-CamSys"
#define CAMSYS_PLATFORM_MARVIN_NAME             "Platform_MarvinDev"
#define CAMSYS_PLATFORM_CIF0_NAME               "Platform_Cif0Dev"
#define CAMSYS_PLATFORM_CIF1_NAME               "Platform_Cif1Dev"

#define CAMSYS_REGISTER_RES_NAME                "CamSys_RegMem"
#define CAMSYS_REGISTER_MIPIPHY_RES_NAME        "CamSys_RegMem_MipiPhy"
#define CAMSYS_IRQ_RES_NAME                     "CamSys_Irq"

#define CAMSYS_REGISTER_MEM_NAME                CAMSYS_REGISTER_RES_NAME
#define CAMSYS_I2C_MEM_NAME                     "CamSys_I2cMem"
#define CAMSYS_MIPIPHY_MEM_NAME                 \
	CAMSYS_REGISTER_MIPIPHY_RES_NAME

#define CAMSYS_NAMELEN_MIN(a)                   \
	((strlen(a) > (CAMSYS_NAME_LEN-1))?(CAMSYS_NAME_LEN-1):strlen(a))
#define CAMSYS_IRQPOOL_NUM                      128
#define CAMSYS_DMA_BUF_MAX_NUM                  32

extern unsigned int camsys_debug;

#define camsys_trace(level, msg, ...) \
	do { \
		if (camsys_debug >= level) \
			printk("D%d:%s(%d): " msg "\n", level,\
			__FUNCTION__, __LINE__, ## __VA_ARGS__); \
	} while (0)

#define camsys_warn(msg, ...)  \
	printk(KERN_ERR "W:%s(%d): " msg "\n", __FUNCTION__,\
	__LINE__, ## __VA_ARGS__)
#define camsys_err(msg, ...)   \
	printk(KERN_ERR "E:%s(%d): " msg "\n", __FUNCTION__,\
	__LINE__, ## __VA_ARGS__)

typedef struct camsys_irqstas_s {
	camsys_irqsta_t       sta;
	struct list_head      list;
} camsys_irqstas_t;

typedef struct camsys_irqpool_s {
	pid_t                 pid;
	unsigned int          timeout;/* us */
	unsigned int          mis;
	unsigned int          icr;
	spinlock_t            lock;/* lock for list */
	camsys_irqstas_t      pool[CAMSYS_IRQPOOL_NUM];
	struct list_head      active;
	struct list_head      deactive;

	struct list_head      list;

	wait_queue_head_t     done;
} camsys_irqpool_t;

typedef struct camsys_irq_s {
	unsigned int          irq_id;
	/* lock for timeout and irq_connect in ioctl */
	spinlock_t            lock;
	struct list_head      irq_pool;
} camsys_irq_t;

typedef struct camsys_meminfo_s {
	unsigned char name[32];
	unsigned long phy_base;
	unsigned long vir_base;
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
	camsys_gpio_t        fl_en;
	void *ext_fsh_dev;
} camsys_flash_t;
typedef struct camsys_extdev_s {
	unsigned char            dev_name[CAMSYS_NAME_LEN];
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
	int (*ops)(void *ptr, camsys_mipiphy_t *phy);
	int (*remove)(struct platform_device *pdev);
} camsys_phyinfo_t;

typedef struct camsys_exdevs_s {
	struct mutex          mut;
	struct list_head      list;
	struct list_head      active;
} camsys_exdevs_t;

typedef struct camsys_dma_buf_s {
	struct dma_buf *dma_buf;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	dma_addr_t dma_addr;
	int fd;
} camsys_dma_buf_t;

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

	camsys_meminfo_t     *csiphy_reg;
	camsys_meminfo_t     *dsiphy_reg;
	camsys_meminfo_t     *isp0_reg;

	unsigned long         rk_grf_base;
	unsigned long         rk_cru_base;
	unsigned long         rk_isp_base;
	atomic_t              refcount;
	struct iommu_domain *domain;
	camsys_dma_buf_t dma_buf[CAMSYS_DMA_BUF_MAX_NUM];
	int dma_buf_cnt;

	int (*clkin_cb)(void *ptr, unsigned int on);
	int (*clkout_cb)(void *ptr, unsigned int on, unsigned int clk);
	int (*reset_cb)(void *ptr, unsigned int on);

	int (*phy_cb)
		(camsys_extdev_t *extdev,
		camsys_sysctrl_t *devctl, void *ptr);
	int (*iomux)(camsys_extdev_t *extdev, void *ptr);
	int (*platform_remove)(struct platform_device *pdev);
	int (*flash_trigger_cb)(void *ptr, int mode, unsigned int on);
	int (*iommu_cb)(void *ptr, camsys_sysctrl_t *devctl);
} camsys_dev_t;


static inline camsys_extdev_t *camsys_find_extdev(
unsigned int dev_id, camsys_dev_t *camsys_dev)
{
	camsys_extdev_t *extdev = NULL;

	if (!list_empty(&camsys_dev->extdevs.list)) {
		list_for_each_entry(extdev,
			&camsys_dev->extdevs.list, list) {
			if (extdev->dev_id == dev_id) {
				return extdev;
			}
		}
	}
	return NULL;
}

static inline camsys_meminfo_t *camsys_find_devmem(
char *name, camsys_dev_t *camsys_dev)
{
	camsys_meminfo_t *devmem;

	if (!list_empty(&camsys_dev->devmems.memslist)) {
		list_for_each_entry(devmem,
			&camsys_dev->devmems.memslist, list) {
			if (strcmp(devmem->name, name) == 0) {
				return devmem;
			}
		}
	}
	camsys_err("%s memory have not been find in %s!",
		name, dev_name(camsys_dev->miscdev.this_device));
	return NULL;
}

static inline int camsys_sysctl_extdev(
camsys_extdev_t *extdev, camsys_sysctrl_t *devctl, camsys_dev_t *camsys_dev)
{
	int err = 0;
	camsys_regulator_t *regulator;
	camsys_gpio_t *gpio;

	if ((devctl->ops > CamSys_Vdd_Start_Tag) &&
		(devctl->ops < CamSys_Vdd_End_Tag)) {
		regulator = &extdev->avdd;
		regulator += devctl->ops-1;

		if (!IS_ERR_OR_NULL(regulator->ldo)) {
			if (devctl->on) {
				err = regulator_set_voltage(
					regulator->ldo, regulator->min_uv,
					regulator->max_uv);
				err |= regulator_enable(regulator->ldo);
				camsys_trace(1,
					"Sysctl %d success, regulator set (%d,%d) uv!",
					devctl->ops, regulator->min_uv,
					regulator->max_uv);
			} else {
				while (regulator_is_enabled(regulator->ldo) > 0)
					regulator_disable(regulator->ldo);
				camsys_trace(1,
					"Sysctl %d success, regulator off!",
					devctl->ops);
			}
		} else {
			err = -EINVAL;
			goto end;
		}
	} else if ((devctl->ops > CamSys_Gpio_Start_Tag) &&
		(devctl->ops < CamSys_Gpio_End_Tag)) {
		gpio = &extdev->pwrdn;
		gpio += devctl->ops - CamSys_Gpio_Start_Tag -1;

		if (gpio->io != 0xffffffff) {
			if (devctl->on) {
				gpio_direction_output(gpio->io, gpio->active);
				gpio_set_value(gpio->io, gpio->active);
				camsys_trace(1,
					"Sysctl %d success, gpio(%d) set %d",
					devctl->ops, gpio->io, gpio->active);
			} else {
				gpio_direction_output(gpio->io, !gpio->active);
				gpio_set_value(gpio->io, !gpio->active);
				camsys_trace(1,
					"Sysctl %d success, gpio(%d) set %d",
					devctl->ops, gpio->io, !gpio->active);
			}
		} else {
			camsys_trace(1, "Sysctl %d not do, because gpio is NULL",
				devctl->ops);
			err = -EINVAL;
			goto end;
		}
	} else if (devctl->ops == CamSys_ClkIn) {
		if (camsys_dev->clkout_cb)
			camsys_dev->clkout_cb
				(camsys_dev, devctl->on,
				extdev->clk.in_rate);
	} else if (devctl->ops == CamSys_Phy) {
		if (camsys_dev->phy_cb)
			(camsys_dev->phy_cb)
				(extdev, devctl,
				(void *)camsys_dev);
	}

end:
	return err;
}

extern struct file_operations camsys_fops;
#endif

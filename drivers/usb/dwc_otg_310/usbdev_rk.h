#ifndef __USBDEV_RK_H
#define __USBDEV_RK_H

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/gpio.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/reset.h>
#include <linux/rockchip/cru.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/cpu.h>

#include "usbdev_grf_regs.h"
#include "usbdev_bc.h"
#include "usbdev_rkuoc.h"

#define USB_PHY_ENABLED (0)
#define USB_PHY_SUSPEND (1)

#define PHY_USB_MODE    (0)
#define PHY_UART_MODE   (1)

#define USB_STATUS_BVABLID    (1)
#define USB_STATUS_DPDM       (2)
#define USB_STATUS_ID         (3)
#define USB_STATUS_UARTMODE   (4)
#define USB_CHIP_ID           (5)
#define USB_REMOTE_WAKEUP     (6)
#define USB_IRQ_WAKEUP        (7)

enum rkusb_rst_flag {
	RST_POR = 0, /* Reset power-on */
	RST_RECNT,	/* Reset re-connect */
	RST_CHN_HALT, /* Reset a channel halt has been detected */
	RST_OTHER,
};

extern int rk_usb_charger_status;
extern void rk_send_wakeup_key(void);
/* rk3188 platform data */
extern struct dwc_otg_platform_data usb20otg_pdata_rk3188;
extern struct dwc_otg_platform_data usb20host_pdata_rk3188;
extern struct rkehci_platform_data rkhsic_pdata_rk3188;
/* rk3288 platform data */
extern struct dwc_otg_platform_data usb20otg_pdata_rk3288;
extern struct dwc_otg_platform_data usb20host_pdata_rk3288;
extern struct rkehci_platform_data rkhsic_pdata_rk3288;
extern struct rkehci_platform_data rkehci_pdata_rk3288;
extern struct rkehci_platform_data rkohci_pdata_rk3288;

struct dwc_otg_platform_data {
	void *privdata;
	struct device *dev;
	struct clk *phyclk;
	struct clk *ahbclk;
	struct clk *busclk;
	struct clk *phyclk_480m;
	int phy_status;
	void (*hw_init) (void);
	void (*phy_suspend) (void *pdata, int suspend);
	void (*soft_reset) (void *pdata, enum rkusb_rst_flag rst_type);
	void (*clock_init) (void *pdata);
	void (*clock_enable) (void *pdata, int enable);
	void (*power_enable) (int enable);
	void (*dwc_otg_uart_mode) (void *pdata, int enter_usb_uart_mode);
	void (*bc_detect_cb) (int bc_type);
	int (*get_status) (int id);
};

struct rkehci_platform_data {
	struct device *dev;
	struct clk *hclk_hsic;
	struct clk *hsic_phy_480m;
	struct clk *hsic_phy_12m;
	struct clk *phyclk;
	struct clk *ahbclk;
	void (*hw_init) (void);
	void (*clock_init) (void *pdata);
	void (*clock_enable) (void *pdata, int enable);
	void (*phy_suspend) (void *pdata, int suspend);
	void (*soft_reset) (void *pdata, enum rkusb_rst_flag rst_type);
	int (*get_status) (int id);
	int clk_status;
	int phy_status;
};

struct dwc_otg_control_usb {
	pGRF_UOC0_REG grf_uoc0_base;
	pGRF_UOC1_REG grf_uoc1_base;
	pGRF_UOC2_REG grf_uoc2_base;
	pGRF_UOC3_REG grf_uoc3_base;
	pGRF_UOC4_REG grf_uoc4_base;
	pGRF_SOC_STATUS_RK3188 grf_soc_status0_rk3188;
	pGRF_SOC_STATUS1_RK3288 grf_soc_status1_rk3288;
	pGRF_SOC_STATUS2_RK3288 grf_soc_status2_rk3288;
	pGRF_SOC_STATUS19_RK3288 grf_soc_status19_rk3288;
	pGRF_SOC_STATUS21_RK3288 grf_soc_status21_rk3288;
	struct gpio *host_gpios;
	struct gpio *otg_gpios;
	struct clk *hclk_usb_peri;
	struct delayed_work usb_det_wakeup_work;
	struct delayed_work usb_charger_det_work;
	struct wake_lock usb_wakelock;
	int remote_wakeup;
	int usb_irq_wakeup;
	int chip_id;
};

enum {
	RK3188_USB_CTLR = 0,	/* rk3188 chip usb */
	RK3288_USB_CTLR,	/* rk3288 chip usb */
};

struct usb20otg_pdata_id {
	char name[32];
	struct dwc_otg_platform_data *pdata;
};

struct usb20host_pdata_id {
	char name[32];
	struct dwc_otg_platform_data *pdata;
};

struct rkehci_pdata_id {
	char name[32];
	struct rkehci_platform_data *pdata;
};
#endif

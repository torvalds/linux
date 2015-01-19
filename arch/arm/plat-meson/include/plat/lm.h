/*
 * arch/arm/plat-meson/include/plat/lm.h
 *
 * Copyright (C) 2010-2014 Amlogic, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __PLAT_MESON_LM_H
#define __PLAT_MESON_LM_H

/** todo:
 *
 * Will move to platform device.
 *
 */

enum usb_port_type_e {
	USB_PORT_TYPE_OTG = 0,
	USB_PORT_TYPE_HOST,
	USB_PORT_TYPE_SLAVE,
};

enum usb_port_speed_e {
	USB_PORT_SPEED_DEFAULT = 0,
	USB_PORT_SPEED_HIGH,
	USB_PORT_SPEED_FULL
};

enum usb_dma_config_e {
	USB_DMA_BURST_DEFAULT = 0,
	USB_DMA_BURST_SINGLE,
	USB_DMA_BURST_INCR,
	USB_DMA_BURST_INCR4,
	USB_DMA_BURST_INCR8,
	USB_DMA_BURST_INCR16,
	USB_DMA_DISABLE,
};

enum usb_phy_id_mode_e {
	USB_PHY_ID_MODE_HW = 0,
	USB_PHY_ID_MODE_SW_HOST,
	USB_PHY_ID_MODE_SW_SLAVE
};

enum usb_port_idx_e {
	USB_PORT_IDX_A = 0,
	USB_PORT_IDX_B,
	USB_PORT_IDX_C,
	USB_PORT_IDX_D
};

enum lm_device_type_e {
	LM_DEVICE_TYPE_USB = 0,
	LM_DEVICE_TYPE_SATA = 1,
};


/* clock settings */
struct lmclock {
	const char * 		name; /* clock name */
	unsigned int		sel; /* clock source selecter, defined in mach/include/xxxclock.h*/
	unsigned int		src; /* input clock freq */
	unsigned int		div; /* clock devider */
};

/* device ralated attributes */
union lmparam {
	struct {
		unsigned int		port_idx;			/* USB_PORT_IDX_A or USB_PORT_IDX_B */
		unsigned int		port_type;		/* OTG/HOST/SLAVE */
		unsigned int		port_speed;		/* Default,High,Full */
		unsigned int		port_config;	/* Reserved */
		unsigned int		dma_config;		/* Default,SINGLE,INCR...*/
		unsigned int		phy_id_mode;	/* HW/SW_HOST/SW_SLAVE */
		unsigned int		phy_tune_reg; /* PHY tune register address */
		void (* set_vbus_power)(char is_power_on);
		void (* charger_detect_cb)(int bc_mode);
	}usb;

	struct {
		unsigned int port_type;
		void (* set_port_power) (char is_power_on);
	}sata;
};

struct lm_device {
	struct device		dev;
	struct resource		*resource;
	unsigned int		irq;
	unsigned int		id;
	unsigned int		type;			/* usb or sata */
	u64				dma_mask_room; /* dma mask room for dev->dma_mask */

	struct lmclock clock;
//	union lmparam param;
	void * pdata;
};

struct lm_driver {
	struct device_driver	drv;
	int			(*probe)(struct lm_device *);
	void			(*remove)(struct lm_device *);
	int			(*suspend)(struct lm_device *, pm_message_t);
	int			(*resume)(struct lm_device *);
};

int lm_driver_register(struct lm_driver *drv);
void lm_driver_unregister(struct lm_driver *drv);

int lm_device_register(struct lm_device *dev);

#define lm_get_drvdata(lm)	((lm)->pdata)
#define lm_set_drvdata(lm,d)	do{ lm->pdata = (void*)d; }while(0)

#define to_lm_device(d) container_of(d, struct lm_device, dev)
#define to_lm_driver(d) container_of(d, struct lm_driver, drv)
//#define lm_get_drvdata(lm)	dev_get_drvdata(&(lm)->dev)
//#define lm_set_drvdata(lm,d)	dev_set_drvdata(&(lm)->dev, d)

#endif //__PLAT_MESON_LM_H
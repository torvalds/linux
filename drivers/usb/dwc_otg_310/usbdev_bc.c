/*
 * Copyright (C) 2013-2014 ROCKCHIP, Inc.
 * Author: LIYUNZHI  <lyz@rock-chips.com>
 * Data: 2014-3-14
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "usbdev_rk.h"

char *bc_string[USB_BC_TYPE_MAX] = {"DISCONNECT",
				    "Stander Downstream Port",
				    "Dedicated Charging Port",
				    "Charging Downstream Port",
				    "UNKNOW"};

/****** GET and SET REGISTER FIELDS IN GRF UOC ******/
#define BC_GET(x) grf_uoc_get_field(&p_bc_uoc_fields[x])
#define BC_SET(x, v) grf_uoc_set_field(&p_bc_uoc_fields[x], v)

uoc_field_t *p_bc_uoc_fields;
static void *p_grf_base;
static void *p_grf_regmap;
DEFINE_MUTEX(bc_mutex);

static enum bc_port_type usb_charger_status = USB_BC_TYPE_DISCNT;

/****** GET REGISTER FIELD INFO FROM Device Tree ******/

static inline void *get_grf_base(struct device_node *np)
{
	void *grf_base = of_iomap(of_get_parent(np), 0);

	if (of_machine_is_compatible("rockchip,rk3188"))
		grf_base -= 0xac;
	else if (of_machine_is_compatible("rockchip,rk3288") ||
		 of_machine_is_compatible("rockchip,rk3288w"))
		grf_base -= 0x284;

	return grf_base;
}

static inline struct regmap *get_grf_regmap(struct device_node *np)
{
	struct regmap *grf;

	grf = syscon_regmap_lookup_by_phandle(of_get_parent(np),
					      "rockchip,grf");
	if (IS_ERR(grf))
		return NULL;
	return grf;
}

void grf_uoc_set_field(uoc_field_t *field, u32 value)
{
	if (!uoc_field_valid(field))
		return;

	if (p_grf_base) {
		grf_uoc_set(p_grf_base, field->b.offset, field->b.bitmap,
			    field->b.mask, value);
	} else if (p_grf_regmap) {
		regmap_grf_uoc_set(p_grf_regmap, field->b.offset,
				   field->b.bitmap,
				   field->b.mask, value);
	}
}

u32 grf_uoc_get_field(uoc_field_t *field)
{
	if (p_grf_base) {
		return grf_uoc_get(p_grf_base, field->b.offset, field->b.bitmap,
				   field->b.mask);
	} else if (p_grf_regmap) {
		return regmap_grf_uoc_get(p_grf_regmap, field->b.offset,
					  field->b.bitmap, field->b.mask);
	} else {
		return 0;
	}
}

static inline int uoc_init_field(struct device_node *np, const char *name,
				 uoc_field_t *f)
{
	of_property_read_u32_array(np, name, f->array, 3);
	/* printk("usb battery charger detect: uoc_init_field: 0x%08x %d %d \n",
	 * 	  f->b.offset,f->b.bitmap,f->b.mask);*/
	return 0;
}

static inline void uoc_init_synop(struct device_node *np)
{
	p_bc_uoc_fields = kcalloc(SYNOP_BC_MAX, sizeof(uoc_field_t),
				  GFP_ATOMIC);

	if (!p_bc_uoc_fields)
		return;

	uoc_init_field(np, "rk_usb,bvalid", &p_bc_uoc_fields[SYNOP_BC_BVALID]);
	uoc_init_field(np, "rk_usb,iddig", &p_bc_uoc_fields[SYNOP_BC_IDDIG]);
	uoc_init_field(np, "rk_usb,dcdenb", &p_bc_uoc_fields[SYNOP_BC_DCDENB]);
	uoc_init_field(np, "rk_usb,vdatsrcenb",
		       &p_bc_uoc_fields[SYNOP_BC_VDATSRCENB]);
	uoc_init_field(np, "rk_usb,vdatdetenb",
		       &p_bc_uoc_fields[SYNOP_BC_VDATDETENB]);
	uoc_init_field(np, "rk_usb,chrgsel",
		       &p_bc_uoc_fields[SYNOP_BC_CHRGSEL]);
	uoc_init_field(np, "rk_usb,chgdet", &p_bc_uoc_fields[SYNOP_BC_CHGDET]);
	uoc_init_field(np, "rk_usb,fsvplus",
		       &p_bc_uoc_fields[SYNOP_BC_FSVPLUS]);
	uoc_init_field(np, "rk_usb,fsvminus",
		       &p_bc_uoc_fields[SYNOP_BC_FSVMINUS]);
}

static inline void uoc_init_rk(struct device_node *np)
{
	p_bc_uoc_fields = kcalloc(RK_BC_MAX, sizeof(uoc_field_t),
				  GFP_ATOMIC);

	if (!p_bc_uoc_fields)
		return;

	uoc_init_field(np, "rk_usb,bvalid", &p_bc_uoc_fields[RK_BC_BVALID]);
	uoc_init_field(np, "rk_usb,iddig", &p_bc_uoc_fields[RK_BC_IDDIG]);
	uoc_init_field(np, "rk_usb,line", &p_bc_uoc_fields[RK_BC_LINESTATE]);
	uoc_init_field(np, "rk_usb,softctrl", &p_bc_uoc_fields[RK_BC_SOFTCTRL]);
	uoc_init_field(np, "rk_usb,opmode", &p_bc_uoc_fields[RK_BC_OPMODE]);
	uoc_init_field(np, "rk_usb,xcvrsel",
		       &p_bc_uoc_fields[RK_BC_XCVRSELECT]);
	uoc_init_field(np, "rk_usb,termsel",
		       &p_bc_uoc_fields[RK_BC_TERMSELECT]);
}

static inline void uoc_init_inno(struct device_node *np)
{
	p_bc_uoc_fields = kcalloc(INNO_BC_MAX, sizeof(uoc_field_t),
				  GFP_ATOMIC);

	if (!p_bc_uoc_fields)
		return;

	uoc_init_field(np, "rk_usb,bvalid",
			   &p_bc_uoc_fields[INNO_BC_BVALID]);
	uoc_init_field(np, "rk_usb,iddig",
			   &p_bc_uoc_fields[INNO_BC_IDDIG]);
	uoc_init_field(np, "rk_usb,vdmsrcen",
			   &p_bc_uoc_fields[INNO_BC_VDMSRCEN]);
	uoc_init_field(np, "rk_usb,vdpsrcen",
			   &p_bc_uoc_fields[INNO_BC_VDPSRCEN]);
	uoc_init_field(np, "rk_usb,rdmpden",
			   &p_bc_uoc_fields[INNO_BC_RDMPDEN]);
	uoc_init_field(np, "rk_usb,idpsrcen",
			   &p_bc_uoc_fields[INNO_BC_IDPSRCEN]);
	uoc_init_field(np, "rk_usb,idmsinken",
			   &p_bc_uoc_fields[INNO_BC_IDMSINKEN]);
	uoc_init_field(np, "rk_usb,idpsinken",
			   &p_bc_uoc_fields[INNO_BC_IDPSINKEN]);
	uoc_init_field(np, "rk_usb,dpattach",
			   &p_bc_uoc_fields[INNO_BC_DPATTACH]);
	uoc_init_field(np, "rk_usb,cpdet",
			   &p_bc_uoc_fields[INNO_BC_CPDET]);
	uoc_init_field(np, "rk_usb,dcpattach",
			   &p_bc_uoc_fields[INNO_BC_DCPATTACH]);
}

/****** BATTERY CHARGER DETECT FUNCTIONS ******/
bool is_connected(void)
{
	if (!p_grf_base && !p_grf_regmap)
		return false;
	if (BC_GET(BC_BVALID) && BC_GET(BC_IDDIG))
		return true;
	return false;
}

enum bc_port_type usb_battery_charger_detect_rk(bool wait)
{

	enum bc_port_type port_type = USB_BC_TYPE_DISCNT;

	if (BC_GET(RK_BC_BVALID) &&
	    BC_GET(RK_BC_IDDIG)) {
		mdelay(10);
		BC_SET(RK_BC_SOFTCTRL, 1);
		BC_SET(RK_BC_OPMODE, 0);
		BC_SET(RK_BC_XCVRSELECT, 1);
		BC_SET(RK_BC_TERMSELECT, 1);

		mdelay(1);
		switch (BC_GET(RK_BC_LINESTATE)) {
		case 1:
			port_type = USB_BC_TYPE_SDP;
			break;

		case 3:
			port_type = USB_BC_TYPE_DCP;
			break;

		default:
			port_type = USB_BC_TYPE_SDP;
			/* printk("%s linestate = %d bad status\n",
			 *	  __func__, BC_GET(RK_BC_LINESTATE)); */
		}

	}
	BC_SET(RK_BC_SOFTCTRL, 0);

	/* printk("%s , battery_charger_detect %d\n",
	 *	  __func__, port_type); */
	return port_type;
}

enum bc_port_type usb_battery_charger_detect_inno(bool wait)
{
	enum bc_port_type port_type = USB_BC_TYPE_DISCNT;
	int dcd_state = DCD_POSITIVE;
	int timeout = 0, i = 0, filted_cpdet = 0;

	/* VBUS Valid detect */
	if (BC_GET(INNO_BC_BVALID) &&
		BC_GET(INNO_BC_IDDIG)) {
		if (wait) {
			/* Do DCD */
			dcd_state = DCD_TIMEOUT;
			BC_SET(INNO_BC_RDMPDEN, 1);
			BC_SET(INNO_BC_IDPSRCEN, 1);
			timeout = T_DCD_TIMEOUT;
			while (timeout--) {
				if (BC_GET(INNO_BC_DPATTACH))
					i++;
				if (i >= 3) {
					/* It is a filter here to assure data
					 * lines contacted for at least 3ms */
					dcd_state = DCD_POSITIVE;
					break;
				}
				mdelay(1);
			}
			BC_SET(INNO_BC_RDMPDEN, 0);
			BC_SET(INNO_BC_IDPSRCEN, 0);
		} else {
			dcd_state = DCD_PASSED;
		}
		if (dcd_state == DCD_TIMEOUT) {
			port_type = USB_BC_TYPE_UNKNOW;
			goto out;
		}

		/* Turn on VDPSRC */
		/* Primary Detection */
		BC_SET(INNO_BC_VDPSRCEN, 1);
		BC_SET(INNO_BC_IDMSINKEN, 1);
		udelay(T_BC_CHGDET_VALID);

		/*
		 * Filter for Primary Detection,
		 * double check CPDET field
		 */
		timeout = T_BC_CHGDET_VALID;
		while(timeout--) {
			/*
			 * In rapidly hotplug case, it's more likely to
			 * distinguish SDP as DCP/CDP because of line
			 * bounce
			 */
			filted_cpdet += (BC_GET(INNO_BC_CPDET) ? 1 : -2);
			udelay(1);
		}
		/* SDP and CDP/DCP distinguish */
		if (filted_cpdet > 0) {
			/* Turn off VDPSRC */
			BC_SET(INNO_BC_VDPSRCEN, 0);
			BC_SET(INNO_BC_IDMSINKEN, 0);

			udelay(T_BC_CHGDET_VALID);

			/* Turn on VDMSRC */
			BC_SET(INNO_BC_VDMSRCEN, 1);
			BC_SET(INNO_BC_IDPSINKEN, 1);
			udelay(T_BC_CHGDET_VALID);
			if (BC_GET(INNO_BC_DCPATTACH))
				port_type = USB_BC_TYPE_DCP;
			else
				port_type = USB_BC_TYPE_CDP;
		} else {
			port_type = USB_BC_TYPE_SDP;
		}

		BC_SET(INNO_BC_VDPSRCEN, 0);
		BC_SET(INNO_BC_IDMSINKEN, 0);
		BC_SET(INNO_BC_VDMSRCEN, 0);
		BC_SET(INNO_BC_IDPSINKEN, 0);

	}
out:
	/*
	printk("%s, Charger type %s, %s DCD, dcd_state = %d\n", __func__,
	       bc_string[port_type], wait ? "wait" : "pass", dcd_state);
	*/
	return port_type;

}

/* When do BC detect PCD pull-up register should be disabled  */
/* wait wait for dcd timeout 900ms */
enum bc_port_type usb_battery_charger_detect_synop(bool wait)
{
	enum bc_port_type port_type = USB_BC_TYPE_DISCNT;
	int dcd_state = DCD_POSITIVE;
	int timeout = 0, i = 0;

	/* VBUS Valid detect */
	if (BC_GET(SYNOP_BC_BVALID) &&
	    BC_GET(SYNOP_BC_IDDIG)) {
		if (wait) {
			/* Do DCD */
			dcd_state = DCD_TIMEOUT;
			BC_SET(SYNOP_BC_DCDENB, 1);
			timeout = T_DCD_TIMEOUT;
			while (timeout--) {
				if (!BC_GET(SYNOP_BC_FSVPLUS))
					i++;
				if (i >= 3) {
					/* It is a filter here to assure data
					 * lines contacted for at least 3ms */
					dcd_state = DCD_POSITIVE;
					break;
				}

				mdelay(1);
			}
			BC_SET(SYNOP_BC_DCDENB, 0);
		} else {
			dcd_state = DCD_PASSED;
		}
		if (dcd_state == DCD_TIMEOUT) {
			port_type = USB_BC_TYPE_UNKNOW;
			goto out;
		}

		/* Turn on VDPSRC */
		/* Primary Detection */
		BC_SET(SYNOP_BC_VDATSRCENB, 1);
		BC_SET(SYNOP_BC_VDATDETENB, 1);
		BC_SET(SYNOP_BC_CHRGSEL, 0);

		udelay(T_BC_CHGDET_VALID);

		/* SDP and CDP/DCP distinguish */
		if (BC_GET(SYNOP_BC_CHGDET)) {
			/* Turn off VDPSRC */
			BC_SET(SYNOP_BC_VDATSRCENB, 0);
			BC_SET(SYNOP_BC_VDATDETENB, 0);

			udelay(T_BC_CHGDET_VALID);

			/* Turn on VDMSRC */
			BC_SET(SYNOP_BC_VDATSRCENB, 1);
			BC_SET(SYNOP_BC_VDATDETENB, 1);
			BC_SET(SYNOP_BC_CHRGSEL, 1);
			udelay(T_BC_CHGDET_VALID);
			if (BC_GET(SYNOP_BC_CHGDET))
				port_type = USB_BC_TYPE_DCP;
			else
				port_type = USB_BC_TYPE_CDP;
		} else {
			port_type = USB_BC_TYPE_SDP;
		}
		BC_SET(SYNOP_BC_VDATSRCENB, 0);
		BC_SET(SYNOP_BC_VDATDETENB, 0);
		BC_SET(SYNOP_BC_CHRGSEL, 0);

	}
out:
	/*
	printk("%s, Charger type %s, %s DCD, dcd_state = %d\n", __func__,
	       bc_string[port_type], wait ? "wait" : "pass", dcd_state);
	*/
	return port_type;
}

enum bc_port_type usb_battery_charger_detect(bool wait)
{
	static struct device_node *np;
	enum bc_port_type ret = USB_BC_TYPE_DISCNT;

	might_sleep();

	if (!np)
		np = of_find_node_by_name(NULL, "usb_bc");
	if (!np)
		return -1;
	if (!p_grf_base && !p_grf_regmap) {
		p_grf_base = get_grf_base(np);
		p_grf_regmap = get_grf_regmap(np);
	}

	mutex_lock(&bc_mutex);
	if (of_device_is_compatible(np, "rockchip,ctrl")) {
		if (!p_bc_uoc_fields)
			uoc_init_rk(np);
		ret = usb_battery_charger_detect_rk(wait);
	}

	else if (of_device_is_compatible(np, "synopsys,phy")) {
		if (!p_bc_uoc_fields)
			uoc_init_synop(np);
		ret = usb_battery_charger_detect_synop(wait);
	}

	else if (of_device_is_compatible(np, "inno,phy")) {
		if (!p_bc_uoc_fields)
			uoc_init_inno(np);
		ret = usb_battery_charger_detect_inno(wait);
	}
	if (ret == USB_BC_TYPE_UNKNOW)
		ret = USB_BC_TYPE_DCP;
	mutex_unlock(&bc_mutex);
	rk_battery_charger_detect_cb(ret);
	return ret;
}

int dwc_otg_check_dpdm(bool wait)
{
	return (is_connected() ? usb_charger_status : USB_BC_TYPE_DISCNT);
}
EXPORT_SYMBOL(dwc_otg_check_dpdm);

/* Call back function for USB charger type changed */
static ATOMIC_NOTIFIER_HEAD(rk_bc_notifier);

int rk_bc_detect_notifier_register(struct notifier_block *nb,
				   enum bc_port_type *type)
{
	*type = (int)usb_battery_charger_detect(0);
	return atomic_notifier_chain_register(&rk_bc_notifier, nb);
}
EXPORT_SYMBOL(rk_bc_detect_notifier_register);

int rk_bc_detect_notifier_unregister(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&rk_bc_notifier, nb);
}
EXPORT_SYMBOL(rk_bc_detect_notifier_unregister);

void rk_bc_detect_notifier_callback(int bc_mode)
{
	atomic_notifier_call_chain(&rk_bc_notifier,bc_mode, NULL);
}

void rk_battery_charger_detect_cb(int new_type)
{
	might_sleep();

	if (usb_charger_status != new_type) {
		printk("%s , battery_charger_detect %d\n", __func__, new_type);
		atomic_notifier_call_chain(&rk_bc_notifier, new_type, NULL);
	}
	mutex_lock(&bc_mutex);
	usb_charger_status = new_type;
	mutex_unlock(&bc_mutex);
}

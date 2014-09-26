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
#define BC_GET(x) grf_uoc_get_field(&pBC_UOC_FIELDS[x])
#define BC_SET(x, v) grf_uoc_set_field(&pBC_UOC_FIELDS[x], v)

uoc_field_t *pBC_UOC_FIELDS;
static void *pGRF_BASE;
int rk_usb_charger_status = USB_BC_TYPE_DISCNT;

/****** GET REGISTER FIELD INFO FROM Device Tree ******/

static inline void *get_grf_base(struct device_node *np)
{
	void *grf_base = of_iomap(of_get_parent(np), 0);

	if (of_machine_is_compatible("rockchip,rk3188"))
		grf_base -= 0xac;
	else if (of_machine_is_compatible("rockchip,rk3288"))
		grf_base -= 0x284;

	return grf_base;
}

void grf_uoc_set_field(uoc_field_t *field, u32 value)
{
	if (!uoc_field_valid(field))
		return;
	grf_uoc_set(pGRF_BASE, field->b.offset, field->b.bitmap, field->b.mask,
		    value);
}

u32 grf_uoc_get_field(uoc_field_t *field)
{
	return grf_uoc_get(pGRF_BASE, field->b.offset, field->b.bitmap,
			   field->b.mask);
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
	pBC_UOC_FIELDS =
	    (uoc_field_t *) kzalloc(SYNOP_BC_MAX * sizeof(uoc_field_t),
				    GFP_ATOMIC);

	uoc_init_field(np, "rk_usb,bvalid", &pBC_UOC_FIELDS[SYNOP_BC_BVALID]);
	uoc_init_field(np, "rk_usb,iddig", &pBC_UOC_FIELDS[SYNOP_BC_IDDIG]);
	uoc_init_field(np, "rk_usb,dcdenb", &pBC_UOC_FIELDS[SYNOP_BC_DCDENB]);
	uoc_init_field(np, "rk_usb,vdatsrcenb",
		       &pBC_UOC_FIELDS[SYNOP_BC_VDATSRCENB]);
	uoc_init_field(np, "rk_usb,vdatdetenb",
		       &pBC_UOC_FIELDS[SYNOP_BC_VDATDETENB]);
	uoc_init_field(np, "rk_usb,chrgsel", &pBC_UOC_FIELDS[SYNOP_BC_CHRGSEL]);
	uoc_init_field(np, "rk_usb,chgdet", &pBC_UOC_FIELDS[SYNOP_BC_CHGDET]);
	uoc_init_field(np, "rk_usb,fsvplus", &pBC_UOC_FIELDS[SYNOP_BC_FSVPLUS]);
	uoc_init_field(np, "rk_usb,fsvminus",
		       &pBC_UOC_FIELDS[SYNOP_BC_FSVMINUS]);
}

static inline void uoc_init_rk(struct device_node *np)
{
	pBC_UOC_FIELDS =
	    (uoc_field_t *) kzalloc(RK_BC_MAX * sizeof(uoc_field_t),
				    GFP_ATOMIC);

	uoc_init_field(np, "rk_usb,bvalid", &pBC_UOC_FIELDS[RK_BC_BVALID]);
	uoc_init_field(np, "rk_usb,iddig", &pBC_UOC_FIELDS[RK_BC_IDDIG]);
	uoc_init_field(np, "rk_usb,line", &pBC_UOC_FIELDS[RK_BC_LINESTATE]);
	uoc_init_field(np, "rk_usb,softctrl", &pBC_UOC_FIELDS[RK_BC_SOFTCTRL]);
	uoc_init_field(np, "rk_usb,opmode", &pBC_UOC_FIELDS[RK_BC_OPMODE]);
	uoc_init_field(np, "rk_usb,xcvrsel", &pBC_UOC_FIELDS[RK_BC_XCVRSELECT]);
	uoc_init_field(np, "rk_usb,termsel", &pBC_UOC_FIELDS[RK_BC_TERMSELECT]);
}

static inline void uoc_init_inno(struct device_node *np)
{
	pBC_UOC_FIELDS = (uoc_field_t *)
			 kzalloc(INNO_BC_MAX * sizeof(uoc_field_t), GFP_ATOMIC);

	uoc_init_field(np, "rk_usb,bvalid",
			   &pBC_UOC_FIELDS[INNO_BC_BVALID]);
	uoc_init_field(np, "rk_usb,iddig",
			   &pBC_UOC_FIELDS[INNO_BC_IDDIG]);
	uoc_init_field(np, "rk_usb,vdmsrcen",
			   &pBC_UOC_FIELDS[INNO_BC_VDMSRCEN]);
	uoc_init_field(np, "rk_usb,vdpsrcen",
			   &pBC_UOC_FIELDS[INNO_BC_VDPSRCEN]);
	uoc_init_field(np, "rk_usb,rdmpden",
			   &pBC_UOC_FIELDS[INNO_BC_RDMPDEN]);
	uoc_init_field(np, "rk_usb,idpsrcen",
			   &pBC_UOC_FIELDS[INNO_BC_IDPSRCEN]);
	uoc_init_field(np, "rk_usb,idmsinken",
			   &pBC_UOC_FIELDS[INNO_BC_IDMSINKEN]);
	uoc_init_field(np, "rk_usb,idpsinken",
			   &pBC_UOC_FIELDS[INNO_BC_IDPSINKEN]);
	uoc_init_field(np, "rk_usb,dpattach",
			   &pBC_UOC_FIELDS[INNO_BC_DPATTACH]);
	uoc_init_field(np, "rk_usb,cpdet",
			   &pBC_UOC_FIELDS[INNO_BC_CPDET]);
	uoc_init_field(np, "rk_usb,dcpattach",
			   &pBC_UOC_FIELDS[INNO_BC_DCPATTACH]);
}

/****** BATTERY CHARGER DETECT FUNCTIONS ******/

int usb_battery_charger_detect_rk(bool wait)
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

int usb_battery_charger_detect_inno(bool wait)
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

		/* SDP and CDP/DCP distinguish */
		if (BC_GET(INNO_BC_CPDET)) {
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
	printk("%s, Charger type %s, %s DCD, dcd_state = %d\n", __func__,
	       bc_string[port_type], wait ? "wait" : "pass", dcd_state);
	return port_type;

}

/* When do BC detect PCD pull-up register should be disabled  */
/* wait wait for dcd timeout 900ms */
int usb_battery_charger_detect_synop(bool wait)
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
	printk("%s, Charger type %s, %s DCD, dcd_state = %d\n", __func__,
	       bc_string[port_type], wait ? "wait" : "pass", dcd_state);
	return port_type;
}

int usb_battery_charger_detect(bool wait)
{
	static struct device_node *np;
	if (!np)
		np = of_find_node_by_name(NULL, "usb_bc");
	if (!np)
		goto fail;
	if (!pGRF_BASE)
		pGRF_BASE = get_grf_base(np);

	if (of_device_is_compatible(np, "rockchip,ctrl")) {
		if (!pBC_UOC_FIELDS)
			uoc_init_rk(np);
		return usb_battery_charger_detect_rk(wait);
	}

	else if (of_device_is_compatible(np, "synopsys,phy")) {
		if (!pBC_UOC_FIELDS)
			uoc_init_synop(np);
		return usb_battery_charger_detect_synop(wait);
	}

	else if (of_device_is_compatible(np, "inno,phy")) {
		if (!pBC_UOC_FIELDS)
			uoc_init_inno(np);
		return usb_battery_charger_detect_inno(wait);
	}
fail:
	return -1;
}

EXPORT_SYMBOL(usb_battery_charger_detect);

int dwc_otg_check_dpdm(bool wait)
{
	static struct device_node *np;
	if (!np)
		np = of_find_node_by_name(NULL, "usb_bc");
	if (!np)
		return -1;
	if (!pGRF_BASE)
		pGRF_BASE = get_grf_base(np);

	if (of_device_is_compatible(np, "rockchip,ctrl")) {
		if (!pBC_UOC_FIELDS)
			uoc_init_rk(np);
		if (!BC_GET(RK_BC_BVALID) ||
		    !BC_GET(RK_BC_IDDIG))
			rk_usb_charger_status = USB_BC_TYPE_DISCNT;

	} else if (of_device_is_compatible(np, "synopsys,phy")) {
		if (!pBC_UOC_FIELDS)
			uoc_init_synop(np);
		if (!BC_GET(SYNOP_BC_BVALID) ||
		    !BC_GET(SYNOP_BC_IDDIG))
			rk_usb_charger_status = USB_BC_TYPE_DISCNT;

	} else if (of_device_is_compatible(np, "inno,phy")) {
		if (!pBC_UOC_FIELDS)
			uoc_init_inno(np);
	}

	return rk_usb_charger_status;
}
EXPORT_SYMBOL(dwc_otg_check_dpdm);

/* CALL BACK FUNCTION for USB CHARGER TYPE CHANGED */

void usb20otg_battery_charger_detect_cb(int charger_type_new)
{
	static int charger_type = USB_BC_TYPE_DISCNT;
	if (charger_type != charger_type_new) {
		switch (charger_type_new) {
		case USB_BC_TYPE_DISCNT:
			break;

		case USB_BC_TYPE_SDP:
			break;

		case USB_BC_TYPE_DCP:
			break;

		case USB_BC_TYPE_CDP:
			break;

		case USB_BC_TYPE_UNKNOW:
			break;

		default:
			break;
		}

		/* printk("%s , battery_charger_detect %d\n",
		 *	  __func__, charger_type_new);*/
	}
	charger_type = charger_type_new;
}

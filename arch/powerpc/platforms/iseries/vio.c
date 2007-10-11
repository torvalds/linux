/*
 * Legacy iSeries specific vio initialisation
 * that needs to be built in (not a module).
 *
 * Â© Copyright 2007 IBM Corporation
 *	Author: Stephen Rothwell
 *	Some parts collected from various other files
 *
 * This program is free software;  you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <linux/of.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/completion.h>
#include <linux/proc_fs.h>

#include <asm/firmware.h>
#include <asm/iseries/vio.h>
#include <asm/iseries/iommu.h>
#include <asm/iseries/hv_types.h>
#include <asm/iseries/hv_lp_event.h>

#define FIRST_VTY	0
#define NUM_VTYS	1
#define FIRST_VSCSI	(FIRST_VTY + NUM_VTYS)
#define NUM_VSCSIS	1
#define FIRST_VLAN	(FIRST_VSCSI + NUM_VSCSIS)
#define NUM_VLANS	HVMAXARCHITECTEDVIRTUALLANS
#define FIRST_VIODASD	(FIRST_VLAN + NUM_VLANS)
#define NUM_VIODASDS	HVMAXARCHITECTEDVIRTUALDISKS
#define FIRST_VIOCD	(FIRST_VIODASD + NUM_VIODASDS)
#define NUM_VIOCDS	HVMAXARCHITECTEDVIRTUALCDROMS
#define FIRST_VIOTAPE	(FIRST_VIOCD + NUM_VIOCDS)
#define NUM_VIOTAPES	HVMAXARCHITECTEDVIRTUALTAPES

struct vio_waitevent {
	struct completion	com;
	int			rc;
	u16			sub_result;
};

struct vio_resource {
	char	rsrcname[10];
	char	type[4];
	char	model[3];
};

static struct property * __init new_property(const char *name, int length,
		const void *value)
{
	struct property *np = kzalloc(sizeof(*np) + strlen(name) + 1 + length,
			GFP_KERNEL);

	if (!np)
		return NULL;
	np->name = (char *)(np + 1);
	np->value = np->name + strlen(name) + 1;
	strcpy(np->name, name);
	memcpy(np->value, value, length);
	np->length = length;
	return np;
}

static void __init free_property(struct property *np)
{
	kfree(np);
}

static struct device_node * __init new_node(const char *path,
		struct device_node *parent)
{
	struct device_node *np = kzalloc(sizeof(*np), GFP_KERNEL);

	if (!np)
		return NULL;
	np->full_name = kmalloc(strlen(path) + 1, GFP_KERNEL);
	if (!np->full_name) {
		kfree(np);
		return NULL;
	}
	strcpy(np->full_name, path);
	of_node_set_flag(np, OF_DYNAMIC);
	kref_init(&np->kref);
	np->parent = of_node_get(parent);
	return np;
}

static void __init free_node(struct device_node *np)
{
	struct property *next;
	struct property *prop;

	next = np->properties;
	while (next) {
		prop = next;
		next = prop->next;
		free_property(prop);
	}
	of_node_put(np->parent);
	kfree(np->full_name);
	kfree(np);
}

static int __init add_string_property(struct device_node *np, const char *name,
		const char *value)
{
	struct property *nprop = new_property(name, strlen(value) + 1, value);

	if (!nprop)
		return 0;
	prom_add_property(np, nprop);
	return 1;
}

static int __init add_raw_property(struct device_node *np, const char *name,
		int length, const void *value)
{
	struct property *nprop = new_property(name, length, value);

	if (!nprop)
		return 0;
	prom_add_property(np, nprop);
	return 1;
}

static void __init handle_cd_event(struct HvLpEvent *event)
{
	struct viocdlpevent *bevent;
	struct vio_waitevent *pwe;

	if (!event)
		/* Notification that a partition went away! */
		return;

	/* First, we should NEVER get an int here...only acks */
	if (hvlpevent_is_int(event)) {
		printk(KERN_WARNING "handle_cd_event: got an unexpected int\n");
		if (hvlpevent_need_ack(event)) {
			event->xRc = HvLpEvent_Rc_InvalidSubtype;
			HvCallEvent_ackLpEvent(event);
		}
		return;
	}

	bevent = (struct viocdlpevent *)event;

	switch (event->xSubtype & VIOMINOR_SUBTYPE_MASK) {
	case viocdgetinfo:
		pwe = (struct vio_waitevent *)event->xCorrelationToken;
		pwe->rc = event->xRc;
		pwe->sub_result = bevent->sub_result;
		complete(&pwe->com);
		break;

	default:
		printk(KERN_WARNING "handle_cd_event: "
			"message with unexpected subtype %0x04X!\n",
			event->xSubtype & VIOMINOR_SUBTYPE_MASK);
		if (hvlpevent_need_ack(event)) {
			event->xRc = HvLpEvent_Rc_InvalidSubtype;
			HvCallEvent_ackLpEvent(event);
		}
	}
}

static void __init get_viocd_info(struct device_node *vio_root)
{
	HvLpEvent_Rc hvrc;
	u32 unit;
	struct vio_waitevent we;
	struct vio_resource *unitinfo;
	dma_addr_t unitinfo_dmaaddr;
	int ret;

	ret = viopath_open(viopath_hostLp, viomajorsubtype_cdio, 2);
	if (ret) {
		printk(KERN_WARNING
			"get_viocd_info: error opening path to host partition %d\n",
			viopath_hostLp);
		return;
	}

	/* Initialize our request handler */
	vio_setHandler(viomajorsubtype_cdio, handle_cd_event);

	unitinfo = iseries_hv_alloc(
			sizeof(*unitinfo) * HVMAXARCHITECTEDVIRTUALCDROMS,
			&unitinfo_dmaaddr, GFP_ATOMIC);
	if (!unitinfo) {
		printk(KERN_WARNING
			"get_viocd_info: error allocating unitinfo\n");
		goto clear_handler;
	}

	memset(unitinfo, 0, sizeof(*unitinfo) * HVMAXARCHITECTEDVIRTUALCDROMS);

	init_completion(&we.com);

	hvrc = HvCallEvent_signalLpEventFast(viopath_hostLp,
			HvLpEvent_Type_VirtualIo,
			viomajorsubtype_cdio | viocdgetinfo,
			HvLpEvent_AckInd_DoAck, HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(viopath_hostLp),
			viopath_targetinst(viopath_hostLp),
			(u64)&we, VIOVERSION << 16, unitinfo_dmaaddr, 0,
			sizeof(*unitinfo) * HVMAXARCHITECTEDVIRTUALCDROMS, 0);
	if (hvrc != HvLpEvent_Rc_Good) {
		printk(KERN_WARNING
			"get_viocd_info: cdrom error sending event. rc %d\n",
			(int)hvrc);
		goto hv_free;
	}

	wait_for_completion(&we.com);

	if (we.rc) {
		printk(KERN_WARNING "get_viocd_info: bad rc %d:0x%04X\n",
			we.rc, we.sub_result);
		goto hv_free;
	}

	for (unit = 0; (unit < HVMAXARCHITECTEDVIRTUALCDROMS) &&
			unitinfo[unit].rsrcname[0]; unit++) {
		struct device_node *np;
		char name[64];
		u32 reg = FIRST_VIOCD + unit;

		snprintf(name, sizeof(name), "/vdevice/viocd@%08x", reg);
		np = new_node(name, vio_root);
		if (!np)
			goto hv_free;
		if (!add_string_property(np, "name", "viocd") ||
			!add_string_property(np, "device_type", "block") ||
			!add_string_property(np, "compatible",
				"IBM,iSeries-viocd") ||
			!add_raw_property(np, "reg", sizeof(reg), &reg) ||
			!add_raw_property(np, "linux,unit_address",
				sizeof(unit), &unit) ||
			!add_raw_property(np, "linux,vio_rsrcname",
				sizeof(unitinfo[unit].rsrcname),
				unitinfo[unit].rsrcname) ||
			!add_raw_property(np, "linux,vio_type",
				sizeof(unitinfo[unit].type),
				unitinfo[unit].type) ||
			!add_raw_property(np, "linux,vio_model",
				sizeof(unitinfo[unit].model),
				unitinfo[unit].model))
			goto node_free;
		np->name = of_get_property(np, "name", NULL);
		np->type = of_get_property(np, "device_type", NULL);
		of_attach_node(np);
#ifdef CONFIG_PROC_DEVICETREE
		if (vio_root->pde) {
			struct proc_dir_entry *ent;

			ent = proc_mkdir(strrchr(np->full_name, '/') + 1,
					vio_root->pde);
			if (ent)
				proc_device_tree_add_node(np, ent);
		}
#endif
		continue;

 node_free:
		free_node(np);
		break;
	}

 hv_free:
	iseries_hv_free(sizeof(*unitinfo) * HVMAXARCHITECTEDVIRTUALCDROMS,
			unitinfo, unitinfo_dmaaddr);
 clear_handler:
	vio_clearHandler(viomajorsubtype_cdio);
	viopath_close(viopath_hostLp, viomajorsubtype_cdio, 2);
}

/* Handle interrupt events for tape */
static void __init handle_tape_event(struct HvLpEvent *event)
{
	struct vio_waitevent *we;
	struct viotapelpevent *tevent = (struct viotapelpevent *)event;

	if (event == NULL)
		/* Notification that a partition went away! */
		return;

	we = (struct vio_waitevent *)event->xCorrelationToken;
	switch (event->xSubtype & VIOMINOR_SUBTYPE_MASK) {
	case viotapegetinfo:
		we->rc = tevent->sub_type_result;
		complete(&we->com);
		break;
	default:
		printk(KERN_WARNING "handle_tape_event: weird ack\n");
	}
}

static void __init get_viotape_info(struct device_node *vio_root)
{
	HvLpEvent_Rc hvrc;
	u32 unit;
	struct vio_resource *unitinfo;
	dma_addr_t unitinfo_dmaaddr;
	size_t len = sizeof(*unitinfo) * HVMAXARCHITECTEDVIRTUALTAPES;
	struct vio_waitevent we;
	int ret;

	ret = viopath_open(viopath_hostLp, viomajorsubtype_tape, 2);
	if (ret) {
		printk(KERN_WARNING "get_viotape_info: "
			"error on viopath_open to hostlp %d\n", ret);
		return;
	}

	vio_setHandler(viomajorsubtype_tape, handle_tape_event);

	unitinfo = iseries_hv_alloc(len, &unitinfo_dmaaddr, GFP_ATOMIC);
	if (!unitinfo)
		goto clear_handler;

	memset(unitinfo, 0, len);

	hvrc = HvCallEvent_signalLpEventFast(viopath_hostLp,
			HvLpEvent_Type_VirtualIo,
			viomajorsubtype_tape | viotapegetinfo,
			HvLpEvent_AckInd_DoAck, HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(viopath_hostLp),
			viopath_targetinst(viopath_hostLp),
			(u64)(unsigned long)&we, VIOVERSION << 16,
			unitinfo_dmaaddr, len, 0, 0);
	if (hvrc != HvLpEvent_Rc_Good) {
		printk(KERN_WARNING "get_viotape_info: hv error on op %d\n",
				(int)hvrc);
		goto hv_free;
	}

	wait_for_completion(&we.com);

	for (unit = 0; (unit < HVMAXARCHITECTEDVIRTUALTAPES) &&
			unitinfo[unit].rsrcname[0]; unit++) {
		struct device_node *np;
		char name[64];
		u32 reg = FIRST_VIOTAPE + unit;

		snprintf(name, sizeof(name), "/vdevice/viotape@%08x", reg);
		np = new_node(name, vio_root);
		if (!np)
			goto hv_free;
		if (!add_string_property(np, "name", "viotape") ||
			!add_string_property(np, "device_type", "byte") ||
			!add_string_property(np, "compatible",
				"IBM,iSeries-viotape") ||
			!add_raw_property(np, "reg", sizeof(reg), &reg) ||
			!add_raw_property(np, "linux,unit_address",
				sizeof(unit), &unit) ||
			!add_raw_property(np, "linux,vio_rsrcname",
				sizeof(unitinfo[unit].rsrcname),
				unitinfo[unit].rsrcname) ||
			!add_raw_property(np, "linux,vio_type",
				sizeof(unitinfo[unit].type),
				unitinfo[unit].type) ||
			!add_raw_property(np, "linux,vio_model",
				sizeof(unitinfo[unit].model),
				unitinfo[unit].model))
			goto node_free;
		np->name = of_get_property(np, "name", NULL);
		np->type = of_get_property(np, "device_type", NULL);
		of_attach_node(np);
#ifdef CONFIG_PROC_DEVICETREE
		if (vio_root->pde) {
			struct proc_dir_entry *ent;

			ent = proc_mkdir(strrchr(np->full_name, '/') + 1,
					vio_root->pde);
			if (ent)
				proc_device_tree_add_node(np, ent);
		}
#endif
		continue;

 node_free:
		free_node(np);
		break;
	}

 hv_free:
	iseries_hv_free(len, unitinfo, unitinfo_dmaaddr);
 clear_handler:
	vio_clearHandler(viomajorsubtype_tape);
	viopath_close(viopath_hostLp, viomajorsubtype_tape, 2);
}

static int __init iseries_vio_init(void)
{
	struct device_node *vio_root;

	if (!firmware_has_feature(FW_FEATURE_ISERIES))
		return -ENODEV;

	iommu_vio_init();

	vio_root = of_find_node_by_path("/vdevice");
	if (!vio_root)
		return -ENODEV;

	if (viopath_hostLp == HvLpIndexInvalid) {
		vio_set_hostlp();
		/* If we don't have a host, bail out */
		if (viopath_hostLp == HvLpIndexInvalid)
			goto put_node;
	}

	get_viocd_info(vio_root);
	get_viotape_info(vio_root);

	return 0;

 put_node:
	of_node_put(vio_root);
	return -ENODEV;
}
arch_initcall(iseries_vio_init);

/*
 * Legacy iSeries specific vio initialisation
 * that needs to be built in (not a module).
 *
 * © Copyright 2007 IBM Corporation
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
#include <linux/module.h>

#include <asm/firmware.h>
#include <asm/vio.h>
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

static struct property *new_property(const char *name, int length,
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

static struct device_node *new_node(const char *path,
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

static void free_node(struct device_node *np)
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

static int add_string_property(struct device_node *np, const char *name,
		const char *value)
{
	struct property *nprop = new_property(name, strlen(value) + 1, value);

	if (!nprop)
		return 0;
	prom_add_property(np, nprop);
	return 1;
}

static int add_raw_property(struct device_node *np, const char *name,
		int length, const void *value)
{
	struct property *nprop = new_property(name, length, value);

	if (!nprop)
		return 0;
	prom_add_property(np, nprop);
	return 1;
}

static struct device_node *do_device_node(struct device_node *parent,
		const char *name, u32 reg, u32 unit, const char *type,
		const char *compat, struct vio_resource *res)
{
	struct device_node *np;
	char path[32];

	snprintf(path, sizeof(path), "/vdevice/%s@%08x", name, reg);
	np = new_node(path, parent);
	if (!np)
		return NULL;
	if (!add_string_property(np, "name", name) ||
		!add_string_property(np, "device_type", type) ||
		!add_string_property(np, "compatible", compat) ||
		!add_raw_property(np, "reg", sizeof(reg), &reg) ||
		!add_raw_property(np, "linux,unit_address",
			sizeof(unit), &unit)) {
		goto node_free;
	}
	if (res) {
		if (!add_raw_property(np, "linux,vio_rsrcname",
				sizeof(res->rsrcname), res->rsrcname) ||
			!add_raw_property(np, "linux,vio_type",
				sizeof(res->type), res->type) ||
			!add_raw_property(np, "linux,vio_model",
				sizeof(res->model), res->model))
			goto node_free;
	}
	np->name = of_get_property(np, "name", NULL);
	np->type = of_get_property(np, "device_type", NULL);
	of_attach_node(np);
#ifdef CONFIG_PROC_DEVICETREE
	if (parent->pde) {
		struct proc_dir_entry *ent;

		ent = proc_mkdir(strrchr(np->full_name, '/') + 1, parent->pde);
		if (ent)
			proc_device_tree_add_node(np, ent);
	}
#endif
	return np;

 node_free:
	free_node(np);
	return NULL;
}

/*
 * This is here so that we can dynamically add viodasd
 * devices without exposing all the above infrastructure.
 */
struct vio_dev *vio_create_viodasd(u32 unit)
{
	struct device_node *vio_root;
	struct device_node *np;
	struct vio_dev *vdev = NULL;

	vio_root = of_find_node_by_path("/vdevice");
	if (!vio_root)
		return NULL;
	np = do_device_node(vio_root, "viodasd", FIRST_VIODASD + unit, unit,
			"block", "IBM,iSeries-viodasd", NULL);
	of_node_put(vio_root);
	if (np) {
		vdev = vio_register_device_node(np);
		if (!vdev)
			free_node(np);
	}
	return vdev;
}
EXPORT_SYMBOL_GPL(vio_create_viodasd);

static void __init handle_block_event(struct HvLpEvent *event)
{
	struct vioblocklpevent *bevent = (struct vioblocklpevent *)event;
	struct vio_waitevent *pwe;

	if (event == NULL)
		/* Notification that a partition went away! */
		return;
	/* First, we should NEVER get an int here...only acks */
	if (hvlpevent_is_int(event)) {
		printk(KERN_WARNING "handle_viod_request: "
		       "Yikes! got an int in viodasd event handler!\n");
		if (hvlpevent_need_ack(event)) {
			event->xRc = HvLpEvent_Rc_InvalidSubtype;
			HvCallEvent_ackLpEvent(event);
		}
		return;
	}

	switch (event->xSubtype & VIOMINOR_SUBTYPE_MASK) {
	case vioblockopen:
		/*
		 * Handle a response to an open request.  We get all the
		 * disk information in the response, so update it.  The
		 * correlation token contains a pointer to a waitevent
		 * structure that has a completion in it.  update the
		 * return code in the waitevent structure and post the
		 * completion to wake up the guy who sent the request
		 */
		pwe = (struct vio_waitevent *)event->xCorrelationToken;
		pwe->rc = event->xRc;
		pwe->sub_result = bevent->sub_result;
		complete(&pwe->com);
		break;
	case vioblockclose:
		break;
	default:
		printk(KERN_WARNING "handle_viod_request: unexpected subtype!");
		if (hvlpevent_need_ack(event)) {
			event->xRc = HvLpEvent_Rc_InvalidSubtype;
			HvCallEvent_ackLpEvent(event);
		}
	}
}

static void __init probe_disk(struct device_node *vio_root, u32 unit)
{
	HvLpEvent_Rc hvrc;
	struct vio_waitevent we;
	u16 flags = 0;

retry:
	init_completion(&we.com);

	/* Send the open event to OS/400 */
	hvrc = HvCallEvent_signalLpEventFast(viopath_hostLp,
			HvLpEvent_Type_VirtualIo,
			viomajorsubtype_blockio | vioblockopen,
			HvLpEvent_AckInd_DoAck, HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(viopath_hostLp),
			viopath_targetinst(viopath_hostLp),
			(u64)(unsigned long)&we, VIOVERSION << 16,
			((u64)unit << 48) | ((u64)flags<< 32),
			0, 0, 0);
	if (hvrc != 0) {
		printk(KERN_WARNING "probe_disk: bad rc on HV open %d\n",
			(int)hvrc);
		return;
	}

	wait_for_completion(&we.com);

	if (we.rc != 0) {
		if (flags != 0)
			return;
		/* try again with read only flag set */
		flags = vioblockflags_ro;
		goto retry;
	}

	/* Send the close event to OS/400.  We DON'T expect a response */
	hvrc = HvCallEvent_signalLpEventFast(viopath_hostLp,
			HvLpEvent_Type_VirtualIo,
			viomajorsubtype_blockio | vioblockclose,
			HvLpEvent_AckInd_NoAck, HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(viopath_hostLp),
			viopath_targetinst(viopath_hostLp),
			0, VIOVERSION << 16,
			((u64)unit << 48) | ((u64)flags << 32),
			0, 0, 0);
	if (hvrc != 0) {
		printk(KERN_WARNING "probe_disk: "
		       "bad rc sending event to OS/400 %d\n", (int)hvrc);
		return;
	}

	do_device_node(vio_root, "viodasd", FIRST_VIODASD + unit, unit,
			"block", "IBM,iSeries-viodasd", NULL);
}

static void __init get_viodasd_info(struct device_node *vio_root)
{
	int rc;
	u32 unit;

	rc = viopath_open(viopath_hostLp, viomajorsubtype_blockio, 2);
	if (rc) {
		printk(KERN_WARNING "get_viodasd_info: "
		       "error opening path to host partition %d\n",
		       viopath_hostLp);
		return;
	}

	/* Initialize our request handler */
	vio_setHandler(viomajorsubtype_blockio, handle_block_event);

	for (unit = 0; unit < HVMAXARCHITECTEDVIRTUALDISKS; unit++)
		probe_disk(vio_root, unit);

	vio_clearHandler(viomajorsubtype_blockio);
	viopath_close(viopath_hostLp, viomajorsubtype_blockio, 2);
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
		if (!do_device_node(vio_root, "viocd", FIRST_VIOCD + unit, unit,
				"block", "IBM,iSeries-viocd", &unitinfo[unit]))
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
		if (!do_device_node(vio_root, "viotape", FIRST_VIOTAPE + unit,
				unit, "byte", "IBM,iSeries-viotape",
				&unitinfo[unit]))
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
	int ret = -ENODEV;

	if (!firmware_has_feature(FW_FEATURE_ISERIES))
		goto out;

	iommu_vio_init();

	vio_root = of_find_node_by_path("/vdevice");
	if (!vio_root)
		goto out;

	if (viopath_hostLp == HvLpIndexInvalid) {
		vio_set_hostlp();
		/* If we don't have a host, bail out */
		if (viopath_hostLp == HvLpIndexInvalid)
			goto put_node;
	}

	get_viodasd_info(vio_root);
	get_viocd_info(vio_root);
	get_viotape_info(vio_root);

	ret = 0;

 put_node:
	of_node_put(vio_root);
 out:
	return ret;
}
arch_initcall(iseries_vio_init);

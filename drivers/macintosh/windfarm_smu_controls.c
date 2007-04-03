/*
 * Windfarm PowerMac thermal control. SMU based controls
 *
 * (c) Copyright 2005 Benjamin Herrenschmidt, IBM Corp.
 *                    <benh@kernel.crashing.org>
 *
 * Released under the term of the GNU GPL v2.
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/completion.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/sections.h>
#include <asm/smu.h>

#include "windfarm.h"

#define VERSION "0.4"

#undef DEBUG

#ifdef DEBUG
#define DBG(args...)	printk(args)
#else
#define DBG(args...)	do { } while(0)
#endif

static int smu_supports_new_fans_ops = 1;

/*
 * SMU fans control object
 */

static LIST_HEAD(smu_fans);

struct smu_fan_control {
	struct list_head	link;
	int    			fan_type;	/* 0 = rpm, 1 = pwm */
	u32			reg;		/* index in SMU */
	s32			value;		/* current value */
	s32			min, max;	/* min/max values */
	struct wf_control	ctrl;
};
#define to_smu_fan(c) container_of(c, struct smu_fan_control, ctrl)

static int smu_set_fan(int pwm, u8 id, u16 value)
{
	struct smu_cmd cmd;
	u8 buffer[16];
	DECLARE_COMPLETION_ONSTACK(comp);
	int rc;

	/* Fill SMU command structure */
	cmd.cmd = SMU_CMD_FAN_COMMAND;

	/* The SMU has an "old" and a "new" way of setting the fan speed
	 * Unfortunately, I found no reliable way to know which one works
	 * on a given machine model. After some investigations it appears
	 * that MacOS X just tries the new one, and if it fails fallbacks
	 * to the old ones ... Ugh.
	 */
 retry:
	if (smu_supports_new_fans_ops) {
		buffer[0] = 0x30;
		buffer[1] = id;
		*((u16 *)(&buffer[2])) = value;
		cmd.data_len = 4;
	} else {
		if (id > 7)
			return -EINVAL;
		/* Fill argument buffer */
		memset(buffer, 0, 16);
		buffer[0] = pwm ? 0x10 : 0x00;
		buffer[1] = 0x01 << id;
		*((u16 *)&buffer[2 + id * 2]) = value;
		cmd.data_len = 14;
	}

	cmd.reply_len = 16;
	cmd.data_buf = cmd.reply_buf = buffer;
	cmd.status = 0;
	cmd.done = smu_done_complete;
	cmd.misc = &comp;

	rc = smu_queue_cmd(&cmd);
	if (rc)
		return rc;
	wait_for_completion(&comp);

	/* Handle fallback (see coment above) */
	if (cmd.status != 0 && smu_supports_new_fans_ops) {
		printk(KERN_WARNING "windfarm: SMU failed new fan command "
		       "falling back to old method\n");
		smu_supports_new_fans_ops = 0;
		goto retry;
	}

	return cmd.status;
}

static void smu_fan_release(struct wf_control *ct)
{
	struct smu_fan_control *fct = to_smu_fan(ct);

	kfree(fct);
}

static int smu_fan_set(struct wf_control *ct, s32 value)
{
	struct smu_fan_control *fct = to_smu_fan(ct);

	if (value < fct->min)
		value = fct->min;
	if (value > fct->max)
		value = fct->max;
	fct->value = value;

	return smu_set_fan(fct->fan_type, fct->reg, value);
}

static int smu_fan_get(struct wf_control *ct, s32 *value)
{
	struct smu_fan_control *fct = to_smu_fan(ct);
	*value = fct->value; /* todo: read from SMU */
	return 0;
}

static s32 smu_fan_min(struct wf_control *ct)
{
	struct smu_fan_control *fct = to_smu_fan(ct);
	return fct->min;
}

static s32 smu_fan_max(struct wf_control *ct)
{
	struct smu_fan_control *fct = to_smu_fan(ct);
	return fct->max;
}

static struct wf_control_ops smu_fan_ops = {
	.set_value	= smu_fan_set,
	.get_value	= smu_fan_get,
	.get_min	= smu_fan_min,
	.get_max	= smu_fan_max,
	.release	= smu_fan_release,
	.owner		= THIS_MODULE,
};

static struct smu_fan_control *smu_fan_create(struct device_node *node,
					      int pwm_fan)
{
	struct smu_fan_control *fct;
	const s32 *v;
	const u32 *reg;
	const char *l;

	fct = kmalloc(sizeof(struct smu_fan_control), GFP_KERNEL);
	if (fct == NULL)
		return NULL;
	fct->ctrl.ops = &smu_fan_ops;
	l = of_get_property(node, "location", NULL);
	if (l == NULL)
		goto fail;

	fct->fan_type = pwm_fan;
	fct->ctrl.type = pwm_fan ? WF_CONTROL_PWM_FAN : WF_CONTROL_RPM_FAN;

	/* We use the name & location here the same way we do for SMU sensors,
	 * see the comment in windfarm_smu_sensors.c. The locations are a bit
	 * less consistent here between the iMac and the desktop models, but
	 * that is good enough for our needs for now at least.
	 *
	 * One problem though is that Apple seem to be inconsistent with case
	 * and the kernel doesn't have strcasecmp =P
	 */

	fct->ctrl.name = NULL;

	/* Names used on desktop models */
	if (!strcmp(l, "Rear Fan 0") || !strcmp(l, "Rear Fan") ||
	    !strcmp(l, "Rear fan 0") || !strcmp(l, "Rear fan") ||
	    !strcmp(l, "CPU A EXHAUST"))
		fct->ctrl.name = "cpu-rear-fan-0";
	else if (!strcmp(l, "Rear Fan 1") || !strcmp(l, "Rear fan 1") ||
		 !strcmp(l, "CPU B EXHAUST"))
		fct->ctrl.name = "cpu-rear-fan-1";
	else if (!strcmp(l, "Front Fan 0") || !strcmp(l, "Front Fan") ||
		 !strcmp(l, "Front fan 0") || !strcmp(l, "Front fan") ||
		 !strcmp(l, "CPU A INTAKE"))
		fct->ctrl.name = "cpu-front-fan-0";
	else if (!strcmp(l, "Front Fan 1") || !strcmp(l, "Front fan 1") ||
		 !strcmp(l, "CPU B INTAKE"))
		fct->ctrl.name = "cpu-front-fan-1";
	else if (!strcmp(l, "CPU A PUMP"))
		fct->ctrl.name = "cpu-pump-0";
	else if (!strcmp(l, "Slots Fan") || !strcmp(l, "Slots fan") ||
		 !strcmp(l, "EXPANSION SLOTS INTAKE"))
		fct->ctrl.name = "slots-fan";
	else if (!strcmp(l, "Drive Bay") || !strcmp(l, "Drive bay") ||
		 !strcmp(l, "DRIVE BAY A INTAKE"))
		fct->ctrl.name = "drive-bay-fan";
	else if (!strcmp(l, "BACKSIDE"))
		fct->ctrl.name = "backside-fan";

	/* Names used on iMac models */
	if (!strcmp(l, "System Fan") || !strcmp(l, "System fan"))
		fct->ctrl.name = "system-fan";
	else if (!strcmp(l, "CPU Fan") || !strcmp(l, "CPU fan"))
		fct->ctrl.name = "cpu-fan";
	else if (!strcmp(l, "Hard Drive") || !strcmp(l, "Hard drive"))
		fct->ctrl.name = "drive-bay-fan";

	/* Unrecognized fan, bail out */
	if (fct->ctrl.name == NULL)
		goto fail;

	/* Get min & max values*/
	v = of_get_property(node, "min-value", NULL);
	if (v == NULL)
		goto fail;
	fct->min = *v;
	v = of_get_property(node, "max-value", NULL);
	if (v == NULL)
		goto fail;
	fct->max = *v;

	/* Get "reg" value */
	reg = of_get_property(node, "reg", NULL);
	if (reg == NULL)
		goto fail;
	fct->reg = *reg;

	if (wf_register_control(&fct->ctrl))
		goto fail;

	return fct;
 fail:
	kfree(fct);
	return NULL;
}


static int __init smu_controls_init(void)
{
	struct device_node *smu, *fans, *fan;

	if (!smu_present())
		return -ENODEV;

	smu = of_find_node_by_type(NULL, "smu");
	if (smu == NULL)
		return -ENODEV;

	/* Look for RPM fans */
	for (fans = NULL; (fans = of_get_next_child(smu, fans)) != NULL;)
		if (!strcmp(fans->name, "rpm-fans") ||
		    device_is_compatible(fans, "smu-rpm-fans"))
			break;
	for (fan = NULL;
	     fans && (fan = of_get_next_child(fans, fan)) != NULL;) {
		struct smu_fan_control *fct;

		fct = smu_fan_create(fan, 0);
		if (fct == NULL) {
			printk(KERN_WARNING "windfarm: Failed to create SMU "
			       "RPM fan %s\n", fan->name);
			continue;
		}
		list_add(&fct->link, &smu_fans);
	}
	of_node_put(fans);


	/* Look for PWM fans */
	for (fans = NULL; (fans = of_get_next_child(smu, fans)) != NULL;)
		if (!strcmp(fans->name, "pwm-fans"))
			break;
	for (fan = NULL;
	     fans && (fan = of_get_next_child(fans, fan)) != NULL;) {
		struct smu_fan_control *fct;

		fct = smu_fan_create(fan, 1);
		if (fct == NULL) {
			printk(KERN_WARNING "windfarm: Failed to create SMU "
			       "PWM fan %s\n", fan->name);
			continue;
		}
		list_add(&fct->link, &smu_fans);
	}
	of_node_put(fans);
	of_node_put(smu);

	return 0;
}

static void __exit smu_controls_exit(void)
{
	struct smu_fan_control *fct;

	while (!list_empty(&smu_fans)) {
		fct = list_entry(smu_fans.next, struct smu_fan_control, link);
		list_del(&fct->link);
		wf_unregister_control(&fct->ctrl);
	}
}


module_init(smu_controls_init);
module_exit(smu_controls_exit);

MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
MODULE_DESCRIPTION("SMU control objects for PowerMacs thermal control");
MODULE_LICENSE("GPL");



/*
 * CAAM/SEC 4.x Security Violation Handler
 * Copyright (C) 2015 Freescale Semiconductor, Inc., All Rights Reserved
 */

#include "compat.h"
#include "intern.h"
#include "secvio.h"
#include "regs.h"
#include <linux/of.h>
#include <linux/of_irq.h>
/*
 * These names are associated with each violation handler.
 * The source names were taken from MX6, and are based on recommendations
 * for most common SoCs.
 */
static const u8 *violation_src_name[] = {
	"CAAM Security Violation",
	"JTAG Alarm",
	"Watchdog",
	"(reserved)",
	"External Boot",
	"Tamper Detect",
};

/* Top-level security violation interrupt */
static irqreturn_t caam_secvio_interrupt(int irq, void *snvsdev)
{
	struct device *dev = snvsdev;
	struct caam_drv_private_secvio *svpriv = dev_get_drvdata(dev);
	u32 irqstate;

	/* Check the HP secvio status register */
	irqstate = rd_reg32(&svpriv->svregs->hp.secvio_status) |
			    HP_SECVIOST_SECVIOMASK;

	if (!irqstate)
		return IRQ_NONE;

	/* Mask out one or more causes for deferred service */
	clrbits32(&svpriv->svregs->hp.secvio_int_ctl, irqstate);

	/* Now ACK causes */
	setbits32(&svpriv->svregs->hp.secvio_status, irqstate);

	/* And run deferred service */
	preempt_disable();
	tasklet_schedule(&svpriv->irqtask[smp_processor_id()]);
	preempt_enable();

	return IRQ_HANDLED;
}

/* Deferred service handler. Tasklet arg is simply the SNVS dev */
static void caam_secvio_dispatch(unsigned long indev)
{
	struct device *dev = (struct device *)indev;
	struct caam_drv_private_secvio *svpriv = dev_get_drvdata(dev);
	unsigned long flags, cause;
	int i;


	/*
	 * Capture the interrupt cause, using masked interrupts as
	 * identification. This only works if all are enabled; if
	  * this changes in the future, a "cause queue" will have to
	 * be built
	 */
	cause = rd_reg32(&svpriv->svregs->hp.secvio_int_ctl) &
			(HP_SECVIO_INTEN_SRC5 | HP_SECVIO_INTEN_SRC4 |
			 HP_SECVIO_INTEN_SRC3 | HP_SECVIO_INTEN_SRC2 |
			 HP_SECVIO_INTEN_SRC1 | HP_SECVIO_INTEN_SRC0);

	/* Look through causes, call each handler if exists */
	for (i = 0; i < MAX_SECVIO_SOURCES; i++)
		if (cause & (1 << i)) {
			spin_lock_irqsave(&svpriv->svlock, flags);
			svpriv->intsrc[i].handler(dev, i,
						  svpriv->intsrc[i].ext);
			spin_unlock_irqrestore(&svpriv->svlock, flags);
		};

	/* Re-enable now-serviced interrupts */
	setbits32(&svpriv->svregs->hp.secvio_int_ctl, cause);
}

/*
 * Default cause handler, used in lieu of an application-defined handler.
 * All it does at this time is print a console message. It could force a halt.
 */
static void caam_secvio_default(struct device *dev, u32 cause, void *ext)
{
	struct caam_drv_private_secvio *svpriv = dev_get_drvdata(dev);

	dev_err(dev, "Unhandled Security Violation Interrupt %d = %s\n",
		cause, svpriv->intsrc[cause].intname);
}

/*
 * Install an application-defined handler for a specified cause
 * Arguments:
 * - dev        points to SNVS-owning device
 * - cause      interrupt source cause
 * - handler    application-defined handler, gets called with dev
 *              source cause, and locally-defined handler argument
 * - cause_description   points to a string to override the default cause
 *                       name, this can be used as an alternate for error
 *                       messages and such. If left NULL, the default
 *                       description string is used.
 * - ext        pointer to any extra data needed by the handler.
 */
int caam_secvio_install_handler(struct device *dev, enum secvio_cause cause,
				void (*handler)(struct device *dev, u32 cause,
						void *ext),
				u8 *cause_description, void *ext)
{
	unsigned long flags;
	struct caam_drv_private_secvio *svpriv;

	svpriv = dev_get_drvdata(dev);

	if ((handler == NULL) || (cause > SECVIO_CAUSE_SOURCE_5))
		return -EINVAL;

	spin_lock_irqsave(&svpriv->svlock, flags);
	svpriv->intsrc[cause].handler = handler;
	if (cause_description != NULL)
		svpriv->intsrc[cause].intname = cause_description;
	if (ext != NULL)
		svpriv->intsrc[cause].ext = ext;
	spin_unlock_irqrestore(&svpriv->svlock, flags);

	return 0;
}
EXPORT_SYMBOL(caam_secvio_install_handler);

/*
 * Remove an application-defined handler for a specified cause (and, by
 * implication, restore the "default".
 * Arguments:
 * - dev	points to SNVS-owning device
 * - cause	interrupt source cause
 */
int caam_secvio_remove_handler(struct device *dev, enum secvio_cause cause)
{
	unsigned long flags;
	struct caam_drv_private_secvio *svpriv;

	svpriv = dev_get_drvdata(dev);

	if (cause > SECVIO_CAUSE_SOURCE_5)
		return -EINVAL;

	spin_lock_irqsave(&svpriv->svlock, flags);
	svpriv->intsrc[cause].intname = violation_src_name[cause];
	svpriv->intsrc[cause].handler = caam_secvio_default;
	svpriv->intsrc[cause].ext = NULL;
	spin_unlock_irqrestore(&svpriv->svlock, flags);
	return 0;
}
EXPORT_SYMBOL(caam_secvio_remove_handler);

int caam_secvio_startup(struct platform_device *pdev)
{
	struct device *ctrldev, *svdev;
	struct caam_drv_private *ctrlpriv;
	struct caam_drv_private_secvio *svpriv;
	struct platform_device *svpdev;
	struct device_node *np;
	const void *prop;
	int i, error, secvio_inten_src;

	ctrldev = &pdev->dev;
	ctrlpriv = dev_get_drvdata(ctrldev);
	/*
	 * Set up the private block for secure memory
	 * Only one instance is possible
	 */
	svpriv = kzalloc(sizeof(struct caam_drv_private_secvio), GFP_KERNEL);
	if (svpriv == NULL) {
		dev_err(ctrldev, "can't alloc private mem for secvio\n");
		return -ENOMEM;
	}
	svpriv->parentdev = ctrldev;

	/* Create the security violation dev */
#ifdef CONFIG_OF

	np = of_find_compatible_node(NULL, NULL, "fsl,imx6q-caam-secvio");
	if (!np)
		return -ENODEV;

	ctrlpriv->secvio_irq = of_irq_to_resource(np, 0, NULL);

	prop = of_get_property(np, "secvio_src", NULL);
	if (prop)
		secvio_inten_src = of_read_ulong(prop, 1);
	else
		secvio_inten_src = HP_SECVIO_INTEN_ALL;

	printk(KERN_ERR "secvio_inten_src = %x\n", secvio_inten_src);

	svpdev = of_platform_device_create(np, NULL, ctrldev);
	if (!svpdev)
		return -ENODEV;

#else
	svpdev = platform_device_register_data(ctrldev, "caam_secvio", 0,
					       svpriv,
				sizeof(struct caam_drv_private_secvio));

	secvio_inten_src = HP_SECVIO_INTEN_ALL;
#endif
	if (svpdev == NULL) {
		kfree(svpriv);
		return -EINVAL;
	}
	svdev = &svpdev->dev;
	dev_set_drvdata(svdev, svpriv);
	ctrlpriv->secviodev = svdev;
	svpriv->svregs = ctrlpriv->snvs;

	/*
	 * Now we have all the dev data set up. Init interrupt
	 * source descriptions
	 */
	for (i = 0; i < MAX_SECVIO_SOURCES; i++) {
		svpriv->intsrc[i].intname = violation_src_name[i];
		svpriv->intsrc[i].handler = caam_secvio_default;
	}

	/* Connect main handler */
	for_each_possible_cpu(i)
		tasklet_init(&svpriv->irqtask[i], caam_secvio_dispatch,
			     (unsigned long)svdev);

	error = request_irq(ctrlpriv->secvio_irq, caam_secvio_interrupt,
			    IRQF_SHARED, "caam_secvio", svdev);
	if (error) {
		dev_err(svdev, "can't connect secvio interrupt\n");
		irq_dispose_mapping(ctrlpriv->secvio_irq);
		ctrlpriv->secvio_irq = 0;
		return -EINVAL;
	}

	/* Enable all sources */
	wr_reg32(&svpriv->svregs->hp.secvio_int_ctl, secvio_inten_src);

	dev_info(svdev, "security violation service handlers armed\n");

	return 0;
}

void caam_secvio_shutdown(struct platform_device *pdev)
{
	struct device *ctrldev, *svdev;
	struct caam_drv_private *priv;
	struct caam_drv_private_secvio *svpriv;
	int i;

	ctrldev = &pdev->dev;
	priv = dev_get_drvdata(ctrldev);
	svdev = priv->secviodev;
	svpriv = dev_get_drvdata(svdev);

	/* Shut off all sources */

	wr_reg32(&svpriv->svregs->hp.secvio_int_ctl, 0);

	/* Remove tasklets and release interrupt */
	for_each_possible_cpu(i)
		tasklet_kill(&svpriv->irqtask[i]);

	free_irq(priv->secvio_irq, svdev);

	kfree(svpriv);
}


#ifdef CONFIG_OF
static void __exit caam_secvio_exit(void)
{
	struct device_node *dev_node;
	struct platform_device *pdev;

	dev_node = of_find_compatible_node(NULL, NULL, "fsl,sec-v4.0");
	if (!dev_node) {
		dev_node = of_find_compatible_node(NULL, NULL, "fsl,sec4.0");
		if (!dev_node)
			return;
	}

	pdev = of_find_device_by_node(dev_node);
	if (!pdev)
		return;

	of_node_get(dev_node);

	caam_secvio_shutdown(pdev);

}

static int __init caam_secvio_init(void)
{
	struct device_node *dev_node;
	struct platform_device *pdev;

	/*
	 * Do of_find_compatible_node() then of_find_device_by_node()
	 * once a functional device tree is available
	 */
	dev_node = of_find_compatible_node(NULL, NULL, "fsl,sec-v4.0");
	if (!dev_node) {
		dev_node = of_find_compatible_node(NULL, NULL,
				"arm,imx6-caam-secvio");
		if (!dev_node)
			return -ENODEV;
	}

	pdev = of_find_device_by_node(dev_node);
	if (!pdev)
		return -ENODEV;

	of_node_put(dev_node);

	return caam_secvio_startup(pdev);
}

module_init(caam_secvio_init);
module_exit(caam_secvio_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("FSL CAAM/SNVS Security Violation Handler");
MODULE_AUTHOR("Freescale Semiconductor - NMSG/MAD");
#endif

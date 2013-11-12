/*
 * CAAM/SEC 4.x transport/backend driver
 * JobR backend functionality
 *
 * Copyright 2008-2012 Freescale Semiconductor, Inc.
 */

#include <linux/of_irq.h>

#include "compat.h"
#include "regs.h"
#include "jr.h"
#include "desc.h"
#include "intern.h"

/* Main per-ring interrupt handler */
static irqreturn_t caam_jr_interrupt(int irq, void *st_dev)
{
	struct device *dev = st_dev;
	struct caam_drv_private_jr *jrp = dev_get_drvdata(dev);
	u32 irqstate;

	/*
	 * Check the output ring for ready responses, kick
	 * tasklet if jobs done.
	 */
	irqstate = rd_reg32(&jrp->rregs->jrintstatus);
	if (!irqstate)
		return IRQ_NONE;

	/*
	 * If JobR error, we got more development work to do
	 * Flag a bug now, but we really need to shut down and
	 * restart the queue (and fix code).
	 */
	if (irqstate & JRINT_JR_ERROR) {
		dev_err(dev, "job ring error: irqstate: %08x\n", irqstate);
		BUG();
	}

	/* mask valid interrupts */
	setbits32(&jrp->rregs->rconfig_lo, JRCFG_IMSK);

	/* Have valid interrupt at this point, just ACK and trigger */
	wr_reg32(&jrp->rregs->jrintstatus, irqstate);

	preempt_disable();
	tasklet_schedule(&jrp->irqtask);
	preempt_enable();

	return IRQ_HANDLED;
}

/* Deferred service handler, run as interrupt-fired tasklet */
static void caam_jr_dequeue(unsigned long devarg)
{
	int hw_idx, sw_idx, i, head, tail;
	struct device *dev = (struct device *)devarg;
	struct caam_drv_private_jr *jrp = dev_get_drvdata(dev);
	void (*usercall)(struct device *dev, u32 *desc, u32 status, void *arg);
	u32 *userdesc, userstatus;
	void *userarg;

	while (rd_reg32(&jrp->rregs->outring_used)) {

		head = ACCESS_ONCE(jrp->head);

		spin_lock(&jrp->outlock);

		sw_idx = tail = jrp->tail;
		hw_idx = jrp->out_ring_read_index;

		for (i = 0; CIRC_CNT(head, tail + i, JOBR_DEPTH) >= 1; i++) {
			sw_idx = (tail + i) & (JOBR_DEPTH - 1);

			smp_read_barrier_depends();

			if (jrp->outring[hw_idx].desc ==
			    jrp->entinfo[sw_idx].desc_addr_dma)
				break; /* found */
		}
		/* we should never fail to find a matching descriptor */
		BUG_ON(CIRC_CNT(head, tail + i, JOBR_DEPTH) <= 0);

		/* Unmap just-run descriptor so we can post-process */
		dma_unmap_single(dev, jrp->outring[hw_idx].desc,
				 jrp->entinfo[sw_idx].desc_size,
				 DMA_TO_DEVICE);

		/* mark completed, avoid matching on a recycled desc addr */
		jrp->entinfo[sw_idx].desc_addr_dma = 0;

		/* Stash callback params for use outside of lock */
		usercall = jrp->entinfo[sw_idx].callbk;
		userarg = jrp->entinfo[sw_idx].cbkarg;
		userdesc = jrp->entinfo[sw_idx].desc_addr_virt;
		userstatus = jrp->outring[hw_idx].jrstatus;

		/* set done */
		wr_reg32(&jrp->rregs->outring_rmvd, 1);

		jrp->out_ring_read_index = (jrp->out_ring_read_index + 1) &
					   (JOBR_DEPTH - 1);

		/*
		 * if this job completed out-of-order, do not increment
		 * the tail.  Otherwise, increment tail by 1 plus the
		 * number of subsequent jobs already completed out-of-order
		 */
		if (sw_idx == tail) {
			do {
				tail = (tail + 1) & (JOBR_DEPTH - 1);
				smp_read_barrier_depends();
			} while (CIRC_CNT(head, tail, JOBR_DEPTH) >= 1 &&
				 jrp->entinfo[tail].desc_addr_dma == 0);

			jrp->tail = tail;
		}

		spin_unlock(&jrp->outlock);

		/* Finally, execute user's callback */
		usercall(dev, userdesc, userstatus, userarg);
	}

	/* reenable / unmask IRQs */
	clrbits32(&jrp->rregs->rconfig_lo, JRCFG_IMSK);
}

/**
 * caam_jr_enqueue() - Enqueue a job descriptor head. Returns 0 if OK,
 * -EBUSY if the queue is full, -EIO if it cannot map the caller's
 * descriptor.
 * @dev:  device of the job ring to be used. This device should have
 *        been assigned prior by caam_jr_register().
 * @desc: points to a job descriptor that execute our request. All
 *        descriptors (and all referenced data) must be in a DMAable
 *        region, and all data references must be physical addresses
 *        accessible to CAAM (i.e. within a PAMU window granted
 *        to it).
 * @cbk:  pointer to a callback function to be invoked upon completion
 *        of this request. This has the form:
 *        callback(struct device *dev, u32 *desc, u32 stat, void *arg)
 *        where:
 *        @dev:    contains the job ring device that processed this
 *                 response.
 *        @desc:   descriptor that initiated the request, same as
 *                 "desc" being argued to caam_jr_enqueue().
 *        @status: untranslated status received from CAAM. See the
 *                 reference manual for a detailed description of
 *                 error meaning, or see the JRSTA definitions in the
 *                 register header file
 *        @areq:   optional pointer to an argument passed with the
 *                 original request
 * @areq: optional pointer to a user argument for use at callback
 *        time.
 **/
int caam_jr_enqueue(struct device *dev, u32 *desc,
		    void (*cbk)(struct device *dev, u32 *desc,
				u32 status, void *areq),
		    void *areq)
{
	struct caam_drv_private_jr *jrp = dev_get_drvdata(dev);
	struct caam_jrentry_info *head_entry;
	int head, tail, desc_size;
	dma_addr_t desc_dma;

	desc_size = (*desc & HDR_JD_LENGTH_MASK) * sizeof(u32);
	desc_dma = dma_map_single(dev, desc, desc_size, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, desc_dma)) {
		dev_err(dev, "caam_jr_enqueue(): can't map jobdesc\n");
		return -EIO;
	}

	spin_lock_bh(&jrp->inplock);

	head = jrp->head;
	tail = ACCESS_ONCE(jrp->tail);

	if (!rd_reg32(&jrp->rregs->inpring_avail) ||
	    CIRC_SPACE(head, tail, JOBR_DEPTH) <= 0) {
		spin_unlock_bh(&jrp->inplock);
		dma_unmap_single(dev, desc_dma, desc_size, DMA_TO_DEVICE);
		return -EBUSY;
	}

	head_entry = &jrp->entinfo[head];
	head_entry->desc_addr_virt = desc;
	head_entry->desc_size = desc_size;
	head_entry->callbk = (void *)cbk;
	head_entry->cbkarg = areq;
	head_entry->desc_addr_dma = desc_dma;

	jrp->inpring[jrp->inp_ring_write_index] = desc_dma;

	smp_wmb();

	jrp->inp_ring_write_index = (jrp->inp_ring_write_index + 1) &
				    (JOBR_DEPTH - 1);
	jrp->head = (head + 1) & (JOBR_DEPTH - 1);

	wr_reg32(&jrp->rregs->inpring_jobadd, 1);

	spin_unlock_bh(&jrp->inplock);

	return 0;
}
EXPORT_SYMBOL(caam_jr_enqueue);

static int caam_reset_hw_jr(struct device *dev)
{
	struct caam_drv_private_jr *jrp = dev_get_drvdata(dev);
	unsigned int timeout = 100000;

	/*
	 * mask interrupts since we are going to poll
	 * for reset completion status
	 */
	setbits32(&jrp->rregs->rconfig_lo, JRCFG_IMSK);

	/* initiate flush (required prior to reset) */
	wr_reg32(&jrp->rregs->jrcommand, JRCR_RESET);
	while (((rd_reg32(&jrp->rregs->jrintstatus) & JRINT_ERR_HALT_MASK) ==
		JRINT_ERR_HALT_INPROGRESS) && --timeout)
		cpu_relax();

	if ((rd_reg32(&jrp->rregs->jrintstatus) & JRINT_ERR_HALT_MASK) !=
	    JRINT_ERR_HALT_COMPLETE || timeout == 0) {
		dev_err(dev, "failed to flush job ring %d\n", jrp->ridx);
		return -EIO;
	}

	/* initiate reset */
	timeout = 100000;
	wr_reg32(&jrp->rregs->jrcommand, JRCR_RESET);
	while ((rd_reg32(&jrp->rregs->jrcommand) & JRCR_RESET) && --timeout)
		cpu_relax();

	if (timeout == 0) {
		dev_err(dev, "failed to reset job ring %d\n", jrp->ridx);
		return -EIO;
	}

	/* unmask interrupts */
	clrbits32(&jrp->rregs->rconfig_lo, JRCFG_IMSK);

	return 0;
}

/*
 * Init JobR independent of platform property detection
 */
static int caam_jr_init(struct device *dev)
{
	struct caam_drv_private_jr *jrp;
	dma_addr_t inpbusaddr, outbusaddr;
	int i, error;

	jrp = dev_get_drvdata(dev);

	tasklet_init(&jrp->irqtask, caam_jr_dequeue, (unsigned long)dev);

	/* Connect job ring interrupt handler. */
	error = request_irq(jrp->irq, caam_jr_interrupt, IRQF_SHARED,
			    "caam-jobr", dev);
	if (error) {
		dev_err(dev, "can't connect JobR %d interrupt (%d)\n",
			jrp->ridx, jrp->irq);
		irq_dispose_mapping(jrp->irq);
		jrp->irq = 0;
		return -EINVAL;
	}

	error = caam_reset_hw_jr(dev);
	if (error)
		return error;

	jrp->inpring = dma_alloc_coherent(dev, sizeof(dma_addr_t) * JOBR_DEPTH,
					  &inpbusaddr, GFP_KERNEL);

	jrp->outring = dma_alloc_coherent(dev, sizeof(struct jr_outentry) *
					  JOBR_DEPTH, &outbusaddr, GFP_KERNEL);

	jrp->entinfo = kzalloc(sizeof(struct caam_jrentry_info) * JOBR_DEPTH,
			       GFP_KERNEL);

	if ((jrp->inpring == NULL) || (jrp->outring == NULL) ||
	    (jrp->entinfo == NULL)) {
		dev_err(dev, "can't allocate job rings for %d\n",
			jrp->ridx);
		return -ENOMEM;
	}

	for (i = 0; i < JOBR_DEPTH; i++)
		jrp->entinfo[i].desc_addr_dma = !0;

	/* Setup rings */
	jrp->inp_ring_write_index = 0;
	jrp->out_ring_read_index = 0;
	jrp->head = 0;
	jrp->tail = 0;

	wr_reg64(&jrp->rregs->inpring_base, inpbusaddr);
	wr_reg64(&jrp->rregs->outring_base, outbusaddr);
	wr_reg32(&jrp->rregs->inpring_size, JOBR_DEPTH);
	wr_reg32(&jrp->rregs->outring_size, JOBR_DEPTH);

	jrp->ringsize = JOBR_DEPTH;

	spin_lock_init(&jrp->inplock);
	spin_lock_init(&jrp->outlock);

	/* Select interrupt coalescing parameters */
	setbits32(&jrp->rregs->rconfig_lo, JOBR_INTC |
		  (JOBR_INTC_COUNT_THLD << JRCFG_ICDCT_SHIFT) |
		  (JOBR_INTC_TIME_THLD << JRCFG_ICTT_SHIFT));

	return 0;
}

/*
 * Shutdown JobR independent of platform property code
 */
int caam_jr_shutdown(struct device *dev)
{
	struct caam_drv_private_jr *jrp = dev_get_drvdata(dev);
	dma_addr_t inpbusaddr, outbusaddr;
	int ret;

	ret = caam_reset_hw_jr(dev);

	tasklet_kill(&jrp->irqtask);

	/* Release interrupt */
	free_irq(jrp->irq, dev);

	/* Free rings */
	inpbusaddr = rd_reg64(&jrp->rregs->inpring_base);
	outbusaddr = rd_reg64(&jrp->rregs->outring_base);
	dma_free_coherent(dev, sizeof(dma_addr_t) * JOBR_DEPTH,
			  jrp->inpring, inpbusaddr);
	dma_free_coherent(dev, sizeof(struct jr_outentry) * JOBR_DEPTH,
			  jrp->outring, outbusaddr);
	kfree(jrp->entinfo);
	of_device_unregister(jrp->jr_pdev);

	return ret;
}

/*
 * Probe routine for each detected JobR subsystem. It assumes that
 * property detection was picked up externally.
 */
int caam_jr_probe(struct platform_device *pdev, struct device_node *np,
		  int ring)
{
	struct device *ctrldev, *jrdev;
	struct platform_device *jr_pdev;
	struct caam_drv_private *ctrlpriv;
	struct caam_drv_private_jr *jrpriv;
	u32 *jroffset;
	int error;

	ctrldev = &pdev->dev;
	ctrlpriv = dev_get_drvdata(ctrldev);

	jrpriv = kmalloc(sizeof(struct caam_drv_private_jr),
			 GFP_KERNEL);
	if (jrpriv == NULL) {
		dev_err(ctrldev, "can't alloc private mem for job ring %d\n",
			ring);
		return -ENOMEM;
	}
	jrpriv->parentdev = ctrldev; /* point back to parent */
	jrpriv->ridx = ring; /* save ring identity relative to detection */

	/*
	 * Derive a pointer to the detected JobRs regs
	 * Driver has already iomapped the entire space, we just
	 * need to add in the offset to this JobR. Don't know if I
	 * like this long-term, but it'll run
	 */
	jroffset = (u32 *)of_get_property(np, "reg", NULL);
	jrpriv->rregs = (struct caam_job_ring __iomem *)((void *)ctrlpriv->ctrl
							 + *jroffset);

	/* Build a local dev for each detected queue */
	jr_pdev = of_platform_device_create(np, NULL, ctrldev);
	if (jr_pdev == NULL) {
		kfree(jrpriv);
		return -EINVAL;
	}

	jrpriv->jr_pdev = jr_pdev;
	jrdev = &jr_pdev->dev;
	dev_set_drvdata(jrdev, jrpriv);
	ctrlpriv->jrdev[ring] = jrdev;

	if (sizeof(dma_addr_t) == sizeof(u64))
		if (of_device_is_compatible(np, "fsl,sec-v5.0-job-ring"))
			dma_set_mask(jrdev, DMA_BIT_MASK(40));
		else
			dma_set_mask(jrdev, DMA_BIT_MASK(36));
	else
		dma_set_mask(jrdev, DMA_BIT_MASK(32));

	/* Identify the interrupt */
	jrpriv->irq = irq_of_parse_and_map(np, 0);

	/* Now do the platform independent part */
	error = caam_jr_init(jrdev); /* now turn on hardware */
	if (error) {
		of_device_unregister(jr_pdev);
		kfree(jrpriv);
		return error;
	}

	return error;
}

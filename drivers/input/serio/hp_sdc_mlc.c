/*
 * Access to HP-HIL MLC through HP System Device Controller.
 *
 * Copyright (c) 2001 Brian S. Julin
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *
 * References:
 * HP-HIL Technical Reference Manual.  Hewlett Packard Product No. 45918A
 * System Device Controller Microprocessor Firmware Theory of Operation
 *      for Part Number 1820-4784 Revision B.  Dwg No. A-1820-4784-2
 *
 */

#include <linux/hil_mlc.h>
#include <linux/hp_sdc.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>

#define PREFIX "HP SDC MLC: "

static hil_mlc hp_sdc_mlc;

MODULE_AUTHOR("Brian S. Julin <bri@calyx.com>");
MODULE_DESCRIPTION("Glue for onboard HIL MLC in HP-PARISC machines");
MODULE_LICENSE("Dual BSD/GPL");

struct hp_sdc_mlc_priv_s {
	int emtestmode;
	hp_sdc_transaction trans;
	u8 tseq[16];
	int got5x;
} hp_sdc_mlc_priv;

/************************* Interrupt context ******************************/
static void hp_sdc_mlc_isr (int irq, void *dev_id, 
			    uint8_t status, uint8_t data) {
  	int idx;
	hil_mlc *mlc = &hp_sdc_mlc;

	write_lock(&(mlc->lock));
	if (mlc->icount < 0) {
		printk(KERN_WARNING PREFIX "HIL Overflow!\n");
		up(&mlc->isem);
		goto out;
	}
	idx = 15 - mlc->icount;
	if ((status & HP_SDC_STATUS_IRQMASK) == HP_SDC_STATUS_HILDATA) {
		mlc->ipacket[idx] |= data | HIL_ERR_INT;
		mlc->icount--;
		if (hp_sdc_mlc_priv.got5x) goto check;
		if (!idx) goto check;
		if ((mlc->ipacket[idx-1] & HIL_PKT_ADDR_MASK) !=
		    (mlc->ipacket[idx] & HIL_PKT_ADDR_MASK)) {
			mlc->ipacket[idx] &= ~HIL_PKT_ADDR_MASK;
			mlc->ipacket[idx] |= (mlc->ipacket[idx-1] 
						    & HIL_PKT_ADDR_MASK);
		}
		goto check;
	}
	/* We know status is 5X */
	if (data & HP_SDC_HIL_ISERR) goto err;
	mlc->ipacket[idx] = 
		(data & HP_SDC_HIL_R1MASK) << HIL_PKT_ADDR_SHIFT;
	hp_sdc_mlc_priv.got5x = 1;
	goto out;

 check:
	hp_sdc_mlc_priv.got5x = 0;
	if (mlc->imatch == 0) goto done;
	if ((mlc->imatch == (HIL_ERR_INT | HIL_PKT_CMD | HIL_CMD_POL)) 
	    && (mlc->ipacket[idx] == (mlc->imatch | idx))) goto done;
	if (mlc->ipacket[idx] == mlc->imatch) goto done;
	goto out;

 err:				
	printk(KERN_DEBUG PREFIX "err code %x\n", data);
	switch (data) {
	case HP_SDC_HIL_RC_DONE:
		printk(KERN_WARNING PREFIX "Bastard SDC reconfigured loop!\n");
		break;
	case HP_SDC_HIL_ERR:
		mlc->ipacket[idx] |= HIL_ERR_INT | HIL_ERR_PERR | 
		  HIL_ERR_FERR | HIL_ERR_FOF;
		break;
	case HP_SDC_HIL_TO:
		mlc->ipacket[idx] |= HIL_ERR_INT | HIL_ERR_LERR;
		break;
	case HP_SDC_HIL_RC:
		printk(KERN_WARNING PREFIX "Bastard SDC decided to reconfigure loop!\n");
		break;
	default:
		printk(KERN_WARNING PREFIX "Unkown HIL Error status (%x)!\n", data);
		break;
	}
	/* No more data will be coming due to an error. */
 done:
	tasklet_schedule(mlc->tasklet);
	up(&(mlc->isem));
 out:
	write_unlock(&(mlc->lock));
}


/******************** Tasklet or userspace context functions ****************/

static int hp_sdc_mlc_in (hil_mlc *mlc, suseconds_t timeout) {
	unsigned long flags;
	struct hp_sdc_mlc_priv_s *priv;
	int rc = 2;

	priv = mlc->priv;

	write_lock_irqsave(&(mlc->lock), flags);

	/* Try to down the semaphore */
	if (down_trylock(&(mlc->isem))) {
		struct timeval tv;
		if (priv->emtestmode) {
			mlc->ipacket[0] = 
				HIL_ERR_INT | (mlc->opacket & 
					       (HIL_PKT_CMD | 
						HIL_PKT_ADDR_MASK | 
						HIL_PKT_DATA_MASK));
			mlc->icount = 14;
			/* printk(KERN_DEBUG PREFIX ">[%x]\n", mlc->ipacket[0]); */
			goto wasup;
		}
		do_gettimeofday(&tv);
		tv.tv_usec += 1000000 * (tv.tv_sec - mlc->instart.tv_sec);
		if (tv.tv_usec - mlc->instart.tv_usec > mlc->intimeout) {
		  /*		  printk("!%i %i", 
				  tv.tv_usec - mlc->instart.tv_usec, 
				  mlc->intimeout);
		  */
			rc = 1;
			up(&(mlc->isem));
		}
		goto done;
	}
 wasup:
	up(&(mlc->isem));
	rc = 0;
	goto done;
 done:
	write_unlock_irqrestore(&(mlc->lock), flags);
	return rc;
}

static int hp_sdc_mlc_cts (hil_mlc *mlc) {
	struct hp_sdc_mlc_priv_s *priv;
	unsigned long flags;

	priv = mlc->priv;	

	write_lock_irqsave(&(mlc->lock), flags);

	/* Try to down the semaphores -- they should be up. */
	if (down_trylock(&(mlc->isem))) {
		BUG();
		goto busy;
	}
	if (down_trylock(&(mlc->osem))) {
	 	BUG();
		up(&(mlc->isem));
		goto busy;
	}
	up(&(mlc->isem));
	up(&(mlc->osem));

	if (down_trylock(&(mlc->csem))) {
		if (priv->trans.act.semaphore != &(mlc->csem)) goto poll;
		goto busy;
	}
	if (!(priv->tseq[4] & HP_SDC_USE_LOOP)) goto done;

 poll:
	priv->trans.act.semaphore = &(mlc->csem);
	priv->trans.actidx = 0;
	priv->trans.idx = 1;
	priv->trans.endidx = 5;
	priv->tseq[0] = 
		HP_SDC_ACT_POSTCMD | HP_SDC_ACT_DATAIN | HP_SDC_ACT_SEMAPHORE;
	priv->tseq[1] = HP_SDC_CMD_READ_USE;
	priv->tseq[2] = 1;
	priv->tseq[3] = 0;
	priv->tseq[4] = 0;
	hp_sdc_enqueue_transaction(&(priv->trans));
 busy:
	write_unlock_irqrestore(&(mlc->lock), flags);
	return 1;
 done:
	priv->trans.act.semaphore = &(mlc->osem);
	up(&(mlc->csem));
	write_unlock_irqrestore(&(mlc->lock), flags);
	return 0;
}

static void hp_sdc_mlc_out (hil_mlc *mlc) {
	struct hp_sdc_mlc_priv_s *priv;
	unsigned long flags;

	priv = mlc->priv;

	write_lock_irqsave(&(mlc->lock), flags);
	
	/* Try to down the semaphore -- it should be up. */
	if (down_trylock(&(mlc->osem))) {
	 	BUG();
		goto done;
	}

	if (mlc->opacket & HIL_DO_ALTER_CTRL) goto do_control;

 do_data:
	if (priv->emtestmode) {
		up(&(mlc->osem));
		goto done;
	}
	/* Shouldn't be sending commands when loop may be busy */
	if (down_trylock(&(mlc->csem))) {
	 	BUG();
		goto done;
	}
	up(&(mlc->csem));

	priv->trans.actidx = 0;
	priv->trans.idx = 1;
	priv->trans.act.semaphore = &(mlc->osem);
	priv->trans.endidx = 6;
	priv->tseq[0] = 
		HP_SDC_ACT_DATAREG | HP_SDC_ACT_POSTCMD | HP_SDC_ACT_SEMAPHORE;
	priv->tseq[1] = 0x7;
	priv->tseq[2] = 
		(mlc->opacket & 
		 (HIL_PKT_ADDR_MASK | HIL_PKT_CMD))
		   >> HIL_PKT_ADDR_SHIFT;
	priv->tseq[3] = 
		(mlc->opacket & HIL_PKT_DATA_MASK) 
		  >> HIL_PKT_DATA_SHIFT;
	priv->tseq[4] = 0;  /* No timeout */
	if (priv->tseq[3] == HIL_CMD_DHR) priv->tseq[4] = 1;
	priv->tseq[5] = HP_SDC_CMD_DO_HIL;
	goto enqueue;

 do_control:
	priv->emtestmode = mlc->opacket & HIL_CTRL_TEST;
	if ((mlc->opacket & (HIL_CTRL_APE | HIL_CTRL_IPF)) == HIL_CTRL_APE) {
		BUG(); /* we cannot emulate this, it should not be used. */
	}
	if ((mlc->opacket & HIL_CTRL_ONLY) == HIL_CTRL_ONLY) goto control_only;
	if (mlc->opacket & HIL_CTRL_APE) { 
		BUG(); /* Should not send command/data after engaging APE */
		goto done;
	}
	/* Disengaging APE this way would not be valid either since 
	 * the loop must be allowed to idle.
	 *
	 * So, it works out that we really never actually send control 
	 * and data when using SDC, we just send the data. 
	 */
	goto do_data;

 control_only:
	priv->trans.actidx = 0;
	priv->trans.idx = 1;
	priv->trans.act.semaphore = &(mlc->osem);
	priv->trans.endidx = 4;
	priv->tseq[0] = 
	  HP_SDC_ACT_PRECMD | HP_SDC_ACT_DATAOUT | HP_SDC_ACT_SEMAPHORE;
	priv->tseq[1] = HP_SDC_CMD_SET_LPC;
	priv->tseq[2] = 1;
	//	priv->tseq[3] = (mlc->ddc + 1) | HP_SDC_LPS_ACSUCC;
	priv->tseq[3] = 0;
	if (mlc->opacket & HIL_CTRL_APE) {
		priv->tseq[3] |= HP_SDC_LPC_APE_IPF;
		down_trylock(&(mlc->csem));
	} 
 enqueue:
	hp_sdc_enqueue_transaction(&(priv->trans));
 done:
	write_unlock_irqrestore(&(mlc->lock), flags);
}

static int __init hp_sdc_mlc_init(void)
{
	hil_mlc *mlc = &hp_sdc_mlc;

	printk(KERN_INFO PREFIX "Registering the System Domain Controller's HIL MLC.\n");

	hp_sdc_mlc_priv.emtestmode = 0;
	hp_sdc_mlc_priv.trans.seq = hp_sdc_mlc_priv.tseq;
	hp_sdc_mlc_priv.trans.act.semaphore = &(mlc->osem);
	hp_sdc_mlc_priv.got5x = 0;

	mlc->cts		= &hp_sdc_mlc_cts;
	mlc->in			= &hp_sdc_mlc_in;
	mlc->out		= &hp_sdc_mlc_out;

	if (hil_mlc_register(mlc)) {
		printk(KERN_WARNING PREFIX "Failed to register MLC structure with hil_mlc\n");
		goto err0;
	}
	mlc->priv		= &hp_sdc_mlc_priv;

	if (hp_sdc_request_hil_irq(&hp_sdc_mlc_isr)) {
		printk(KERN_WARNING PREFIX "Request for raw HIL ISR hook denied\n");
		goto err1;
	}
	return 0;
 err1:
	if (hil_mlc_unregister(mlc)) {
		printk(KERN_ERR PREFIX "Failed to unregister MLC structure with hil_mlc.\n"
			"This is bad.  Could cause an oops.\n");
	}
 err0:
	return -EBUSY;
}

static void __exit hp_sdc_mlc_exit(void)
{
	hil_mlc *mlc = &hp_sdc_mlc;
	if (hp_sdc_release_hil_irq(&hp_sdc_mlc_isr)) {
		printk(KERN_ERR PREFIX "Failed to release the raw HIL ISR hook.\n"
			"This is bad.  Could cause an oops.\n");
	}
	if (hil_mlc_unregister(mlc)) {
		printk(KERN_ERR PREFIX "Failed to unregister MLC structure with hil_mlc.\n"
			"This is bad.  Could cause an oops.\n");
	}
}

module_init(hp_sdc_mlc_init);
module_exit(hp_sdc_mlc_exit);

/*
 * drivers/atm/suni.c - S/UNI PHY driver
 *
 * Supports the following:
 * 	PMC PM5346 S/UNI LITE
 * 	PMC PM5350 S/UNI 155 ULTRA
 * 	PMC PM5355 S/UNI 622
 */
 
/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */

#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/atmdev.h>
#include <linux/sonet.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/capability.h>
#include <linux/atm_suni.h>
#include <asm/system.h>
#include <asm/param.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>

#include "suni.h"


#if 0
#define DPRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define DPRINTK(format,args...)
#endif

#define PRIV(dev) ((struct suni_priv *) dev->phy_data)

#define PUT(val,reg) dev->ops->phy_put(dev,val,SUNI_##reg)
#define GET(reg) dev->ops->phy_get(dev,SUNI_##reg)
#define REG_CHANGE(mask,shift,value,reg) \
  PUT((GET(reg) & ~(mask)) | ((value) << (shift)),reg)


static struct timer_list poll_timer;
static struct suni_priv *sunis = NULL;
static DEFINE_SPINLOCK(sunis_lock);


#define ADD_LIMITED(s,v) \
    atomic_add((v),&stats->s); \
    if (atomic_read(&stats->s) < 0) atomic_set(&stats->s,INT_MAX);


static void suni_hz(unsigned long from_timer)
{
	struct suni_priv *walk;
	struct atm_dev *dev;
	struct k_sonet_stats *stats;

	for (walk = sunis; walk; walk = walk->next) {
		dev = walk->dev;
		stats = &walk->sonet_stats;
		PUT(0,MRI); /* latch counters */
		udelay(1);
		ADD_LIMITED(section_bip,(GET(RSOP_SBL) & 0xff) |
		    ((GET(RSOP_SBM) & 0xff) << 8));
		ADD_LIMITED(line_bip,(GET(RLOP_LBL) & 0xff) |
		    ((GET(RLOP_LB) & 0xff) << 8) |
		    ((GET(RLOP_LBM) & 0xf) << 16));
		ADD_LIMITED(path_bip,(GET(RPOP_PBL) & 0xff) |
		    ((GET(RPOP_PBM) & 0xff) << 8));
		ADD_LIMITED(line_febe,(GET(RLOP_LFL) & 0xff) |
		    ((GET(RLOP_LF) & 0xff) << 8) |
		    ((GET(RLOP_LFM) & 0xf) << 16));
		ADD_LIMITED(path_febe,(GET(RPOP_PFL) & 0xff) |
		    ((GET(RPOP_PFM) & 0xff) << 8));
		ADD_LIMITED(corr_hcs,GET(RACP_CHEC) & 0xff);
		ADD_LIMITED(uncorr_hcs,GET(RACP_UHEC) & 0xff);
		ADD_LIMITED(rx_cells,(GET(RACP_RCCL) & 0xff) |
		    ((GET(RACP_RCC) & 0xff) << 8) |
		    ((GET(RACP_RCCM) & 7) << 16));
		ADD_LIMITED(tx_cells,(GET(TACP_TCCL) & 0xff) |
		    ((GET(TACP_TCC) & 0xff) << 8) |
		    ((GET(TACP_TCCM) & 7) << 16));
	}
	if (from_timer) mod_timer(&poll_timer,jiffies+HZ);
}


#undef ADD_LIMITED


static int fetch_stats(struct atm_dev *dev,struct sonet_stats __user *arg,int zero)
{
	struct sonet_stats tmp;
	int error = 0;

	sonet_copy_stats(&PRIV(dev)->sonet_stats,&tmp);
	if (arg) error = copy_to_user(arg,&tmp,sizeof(tmp));
	if (zero && !error) sonet_subtract_stats(&PRIV(dev)->sonet_stats,&tmp);
	return error ? -EFAULT : 0;
}


#define HANDLE_FLAG(flag,reg,bit) \
  if (todo & flag) { \
    if (set) PUT(GET(reg) | bit,reg); \
    else PUT(GET(reg) & ~bit,reg); \
    todo &= ~flag; \
  }


static int change_diag(struct atm_dev *dev,void __user *arg,int set)
{
	int todo;

	if (get_user(todo,(int __user *)arg)) return -EFAULT;
	HANDLE_FLAG(SONET_INS_SBIP,TSOP_DIAG,SUNI_TSOP_DIAG_DBIP8);
	HANDLE_FLAG(SONET_INS_LBIP,TLOP_DIAG,SUNI_TLOP_DIAG_DBIP);
	HANDLE_FLAG(SONET_INS_PBIP,TPOP_CD,SUNI_TPOP_DIAG_DB3);
	HANDLE_FLAG(SONET_INS_FRAME,RSOP_CIE,SUNI_RSOP_CIE_FOOF);
	HANDLE_FLAG(SONET_INS_LAIS,TSOP_CTRL,SUNI_TSOP_CTRL_LAIS);
	HANDLE_FLAG(SONET_INS_PAIS,TPOP_CD,SUNI_TPOP_DIAG_PAIS);
	HANDLE_FLAG(SONET_INS_LOS,TSOP_DIAG,SUNI_TSOP_DIAG_DLOS);
	HANDLE_FLAG(SONET_INS_HCS,TACP_CS,SUNI_TACP_CS_DHCS);
	return put_user(todo,(int __user *)arg) ? -EFAULT : 0;
}


#undef HANDLE_FLAG


static int get_diag(struct atm_dev *dev,void __user *arg)
{
	int set;

	set = 0;
	if (GET(TSOP_DIAG) & SUNI_TSOP_DIAG_DBIP8) set |= SONET_INS_SBIP;
	if (GET(TLOP_DIAG) & SUNI_TLOP_DIAG_DBIP) set |= SONET_INS_LBIP;
	if (GET(TPOP_CD) & SUNI_TPOP_DIAG_DB3) set |= SONET_INS_PBIP;
	/* SONET_INS_FRAME is one-shot only */
	if (GET(TSOP_CTRL) & SUNI_TSOP_CTRL_LAIS) set |= SONET_INS_LAIS;
	if (GET(TPOP_CD) & SUNI_TPOP_DIAG_PAIS) set |= SONET_INS_PAIS;
	if (GET(TSOP_DIAG) & SUNI_TSOP_DIAG_DLOS) set |= SONET_INS_LOS;
	if (GET(TACP_CS) & SUNI_TACP_CS_DHCS) set |= SONET_INS_HCS;
	return put_user(set,(int __user *)arg) ? -EFAULT : 0;
}


static int set_loopback(struct atm_dev *dev,int mode)
{
	unsigned char control;
	int reg, dle, lle;

	if (PRIV(dev)->type == SUNI_MRI_TYPE_PM5355) {
		reg = SUNI_MCM;
		dle = SUNI_MCM_DLE;
		lle = SUNI_MCM_LLE;
	} else {
		reg = SUNI_MCT;
		dle = SUNI_MCT_DLE;
		lle = SUNI_MCT_LLE;
	}

	control = dev->ops->phy_get(dev, reg) & ~(dle | lle);
	switch (mode) {
		case ATM_LM_NONE:
			break;
		case ATM_LM_LOC_PHY:
			control |= dle;
			break;
		case ATM_LM_RMT_PHY:
			control |= lle;
			break;
		default:
			return -EINVAL;
	}
	 dev->ops->phy_put(dev, control, reg);
	PRIV(dev)->loop_mode = mode;
	return 0;
}

/*
 * SONET vs. SDH Configuration
 *
 * Z0INS (register 0x06): 0 for SONET, 1 for SDH
 * ENSS (register 0x3D): 0 for SONET, 1 for SDH
 * LEN16 (register 0x28): 0 for SONET, 1 for SDH (n/a for S/UNI 155 QUAD)
 * LEN16 (register 0x50): 0 for SONET, 1 for SDH (n/a for S/UNI 155 QUAD)
 * S[1:0] (register 0x46): 00 for SONET, 10 for SDH
 */

static int set_sonet(struct atm_dev *dev)
{
	if (PRIV(dev)->type == SUNI_MRI_TYPE_PM5355) {
		PUT(GET(RPOP_RC) & ~SUNI_RPOP_RC_ENSS, RPOP_RC);
		PUT(GET(SSTB_CTRL) & ~SUNI_SSTB_CTRL_LEN16, SSTB_CTRL);
		PUT(GET(SPTB_CTRL) & ~SUNI_SPTB_CTRL_LEN16, SPTB_CTRL);
	}

	REG_CHANGE(SUNI_TPOP_APM_S, SUNI_TPOP_APM_S_SHIFT,
		   SUNI_TPOP_S_SONET, TPOP_APM);

	return 0;
}

static int set_sdh(struct atm_dev *dev)
{
	if (PRIV(dev)->type == SUNI_MRI_TYPE_PM5355) {
		PUT(GET(RPOP_RC) | SUNI_RPOP_RC_ENSS, RPOP_RC);
		PUT(GET(SSTB_CTRL) | SUNI_SSTB_CTRL_LEN16, SSTB_CTRL);
		PUT(GET(SPTB_CTRL) | SUNI_SPTB_CTRL_LEN16, SPTB_CTRL);
	}

	REG_CHANGE(SUNI_TPOP_APM_S, SUNI_TPOP_APM_S_SHIFT,
		   SUNI_TPOP_S_SDH, TPOP_APM);

	return 0;
}


static int get_framing(struct atm_dev *dev, void __user *arg)
{
	int framing;
	unsigned char s;


	s = (GET(TPOP_APM) & SUNI_TPOP_APM_S) >> SUNI_TPOP_APM_S_SHIFT;
	if (s == SUNI_TPOP_S_SONET)
		framing = SONET_FRAME_SONET;
	else
		framing = SONET_FRAME_SDH;

	return put_user(framing, (int __user *) arg) ? -EFAULT : 0;
}

static int set_framing(struct atm_dev *dev, void __user *arg)
{
	int mode;

	if (get_user(mode, (int __user *) arg))
		return -EFAULT;

	if (mode == SONET_FRAME_SONET)
		return set_sonet(dev);
	else if (mode == SONET_FRAME_SDH)
		return set_sdh(dev);

	return -EINVAL;
}


static int suni_ioctl(struct atm_dev *dev,unsigned int cmd,void __user *arg)
{
	switch (cmd) {
		case SONET_GETSTATZ:
		case SONET_GETSTAT:
			return fetch_stats(dev, arg, cmd == SONET_GETSTATZ);
		case SONET_SETDIAG:
			return change_diag(dev,arg,1);
		case SONET_CLRDIAG:
			return change_diag(dev,arg,0);
		case SONET_GETDIAG:
			return get_diag(dev,arg);
		case SONET_SETFRAMING:
			if (!capable(CAP_NET_ADMIN))
				return -EPERM;
			return set_framing(dev, arg);
		case SONET_GETFRAMING:
			return get_framing(dev, arg);
		case SONET_GETFRSENSE:
			return -EINVAL;
		case ATM_SETLOOP:
			if (!capable(CAP_NET_ADMIN))
				return -EPERM;
			return set_loopback(dev,(int)(unsigned long)arg);
		case ATM_GETLOOP:
			return put_user(PRIV(dev)->loop_mode,(int __user *)arg) ?
			    -EFAULT : 0;
		case ATM_QUERYLOOP:
			return put_user(ATM_LM_LOC_PHY | ATM_LM_RMT_PHY,
			    (int __user *) arg) ? -EFAULT : 0;
		default:
			return -ENOIOCTLCMD;
	}
}


static void poll_los(struct atm_dev *dev)
{
	dev->signal = GET(RSOP_SIS) & SUNI_RSOP_SIS_LOSV ? ATM_PHY_SIG_LOST :
	  ATM_PHY_SIG_FOUND;
}


static void suni_int(struct atm_dev *dev)
{
	poll_los(dev);
	printk(KERN_NOTICE "%s(itf %d): signal %s\n",dev->type,dev->number,
	    dev->signal == ATM_PHY_SIG_LOST ?  "lost" : "detected again");
}


static int suni_start(struct atm_dev *dev)
{
	unsigned long flags;
	int first;

	spin_lock_irqsave(&sunis_lock,flags);
	first = !sunis;
	PRIV(dev)->next = sunis;
	sunis = PRIV(dev);
	spin_unlock_irqrestore(&sunis_lock,flags);
	memset(&PRIV(dev)->sonet_stats,0,sizeof(struct k_sonet_stats));
	PUT(GET(RSOP_CIE) | SUNI_RSOP_CIE_LOSE,RSOP_CIE);
		/* interrupt on loss of signal */
	poll_los(dev); /* ... and clear SUNI interrupts */
	if (dev->signal == ATM_PHY_SIG_LOST)
		printk(KERN_WARNING "%s(itf %d): no signal\n",dev->type,
		    dev->number);
	PRIV(dev)->loop_mode = ATM_LM_NONE;
	suni_hz(0); /* clear SUNI counters */
	(void) fetch_stats(dev,NULL,1); /* clear kernel counters */
	if (first) {
		init_timer(&poll_timer);
		poll_timer.expires = jiffies+HZ;
		poll_timer.function = suni_hz;
		poll_timer.data = 1;
#if 0
printk(KERN_DEBUG "[u] p=0x%lx,n=0x%lx\n",(unsigned long) poll_timer.list.prev,
    (unsigned long) poll_timer.list.next);
#endif
		add_timer(&poll_timer);
	}
	return 0;
}


static int suni_stop(struct atm_dev *dev)
{
	struct suni_priv **walk;
	unsigned long flags;

	/* let SAR driver worry about stopping interrupts */
	spin_lock_irqsave(&sunis_lock,flags);
	for (walk = &sunis; *walk != PRIV(dev);
	    walk = &PRIV((*walk)->dev)->next);
	*walk = PRIV((*walk)->dev)->next;
	if (!sunis) del_timer_sync(&poll_timer);
	spin_unlock_irqrestore(&sunis_lock,flags);
	kfree(PRIV(dev));

	return 0;
}


static const struct atmphy_ops suni_ops = {
	.start		= suni_start,
	.ioctl		= suni_ioctl,
	.interrupt	= suni_int,
	.stop		= suni_stop,
};


int suni_init(struct atm_dev *dev)
{
	unsigned char mri;

	if (!(dev->phy_data = kmalloc(sizeof(struct suni_priv),GFP_KERNEL)))
		return -ENOMEM;
	PRIV(dev)->dev = dev;

	mri = GET(MRI); /* reset SUNI */
	PRIV(dev)->type = (mri & SUNI_MRI_TYPE) >> SUNI_MRI_TYPE_SHIFT;
	PUT(mri | SUNI_MRI_RESET,MRI);
	PUT(mri,MRI);
	PUT((GET(MT) & SUNI_MT_DS27_53),MT); /* disable all tests */
        set_sonet(dev);
	REG_CHANGE(SUNI_TACP_IUCHP_CLP,0,SUNI_TACP_IUCHP_CLP,
	    TACP_IUCHP); /* idle cells */
	PUT(SUNI_IDLE_PATTERN,TACP_IUCPOP);
	dev->phy = &suni_ops;

	return 0;
}

EXPORT_SYMBOL(suni_init);

MODULE_LICENSE("GPL");

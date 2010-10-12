/******************************************************************* 
 *
 * Copyright (c) 2000 ATecoM GmbH 
 *
 * The author may be reached at ecd@atecom.com.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR   IMPLIED
 * WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT,  INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 * USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *******************************************************************/

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/poison.h>
#include <linux/skbuff.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/netdevice.h>
#include <linux/atmdev.h>
#include <linux/atm.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/wait.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <asm/byteorder.h>

#ifdef CONFIG_ATM_IDT77252_USE_SUNI
#include "suni.h"
#endif /* CONFIG_ATM_IDT77252_USE_SUNI */


#include "idt77252.h"
#include "idt77252_tables.h"

static unsigned int vpibits = 1;


#define ATM_IDT77252_SEND_IDLE 1


/*
 * Debug HACKs.
 */
#define DEBUG_MODULE 1
#undef HAVE_EEPROM	/* does not work, yet. */

#ifdef CONFIG_ATM_IDT77252_DEBUG
static unsigned long debug = DBG_GENERAL;
#endif


#define SAR_RX_DELAY	(SAR_CFG_RXINT_NODELAY)


/*
 * SCQ Handling.
 */
static struct scq_info *alloc_scq(struct idt77252_dev *, int);
static void free_scq(struct idt77252_dev *, struct scq_info *);
static int queue_skb(struct idt77252_dev *, struct vc_map *,
		     struct sk_buff *, int oam);
static void drain_scq(struct idt77252_dev *, struct vc_map *);
static unsigned long get_free_scd(struct idt77252_dev *, struct vc_map *);
static void fill_scd(struct idt77252_dev *, struct scq_info *, int);

/*
 * FBQ Handling.
 */
static int push_rx_skb(struct idt77252_dev *,
		       struct sk_buff *, int queue);
static void recycle_rx_skb(struct idt77252_dev *, struct sk_buff *);
static void flush_rx_pool(struct idt77252_dev *, struct rx_pool *);
static void recycle_rx_pool_skb(struct idt77252_dev *,
				struct rx_pool *);
static void add_rx_skb(struct idt77252_dev *, int queue,
		       unsigned int size, unsigned int count);

/*
 * RSQ Handling.
 */
static int init_rsq(struct idt77252_dev *);
static void deinit_rsq(struct idt77252_dev *);
static void idt77252_rx(struct idt77252_dev *);

/*
 * TSQ handling.
 */
static int init_tsq(struct idt77252_dev *);
static void deinit_tsq(struct idt77252_dev *);
static void idt77252_tx(struct idt77252_dev *);


/*
 * ATM Interface.
 */
static void idt77252_dev_close(struct atm_dev *dev);
static int idt77252_open(struct atm_vcc *vcc);
static void idt77252_close(struct atm_vcc *vcc);
static int idt77252_send(struct atm_vcc *vcc, struct sk_buff *skb);
static int idt77252_send_oam(struct atm_vcc *vcc, void *cell,
			     int flags);
static void idt77252_phy_put(struct atm_dev *dev, unsigned char value,
			     unsigned long addr);
static unsigned char idt77252_phy_get(struct atm_dev *dev, unsigned long addr);
static int idt77252_change_qos(struct atm_vcc *vcc, struct atm_qos *qos,
			       int flags);
static int idt77252_proc_read(struct atm_dev *dev, loff_t * pos,
			      char *page);
static void idt77252_softint(struct work_struct *work);


static struct atmdev_ops idt77252_ops =
{
	.dev_close	= idt77252_dev_close,
	.open		= idt77252_open,
	.close		= idt77252_close,
	.send		= idt77252_send,
	.send_oam	= idt77252_send_oam,
	.phy_put	= idt77252_phy_put,
	.phy_get	= idt77252_phy_get,
	.change_qos	= idt77252_change_qos,
	.proc_read	= idt77252_proc_read,
	.owner		= THIS_MODULE
};

static struct idt77252_dev *idt77252_chain = NULL;
static unsigned int idt77252_sram_write_errors = 0;

/*****************************************************************************/
/*                                                                           */
/* I/O and Utility Bus                                                       */
/*                                                                           */
/*****************************************************************************/

static void
waitfor_idle(struct idt77252_dev *card)
{
	u32 stat;

	stat = readl(SAR_REG_STAT);
	while (stat & SAR_STAT_CMDBZ)
		stat = readl(SAR_REG_STAT);
}

static u32
read_sram(struct idt77252_dev *card, unsigned long addr)
{
	unsigned long flags;
	u32 value;

	spin_lock_irqsave(&card->cmd_lock, flags);
	writel(SAR_CMD_READ_SRAM | (addr << 2), SAR_REG_CMD);
	waitfor_idle(card);
	value = readl(SAR_REG_DR0);
	spin_unlock_irqrestore(&card->cmd_lock, flags);
	return value;
}

static void
write_sram(struct idt77252_dev *card, unsigned long addr, u32 value)
{
	unsigned long flags;

	if ((idt77252_sram_write_errors == 0) &&
	    (((addr > card->tst[0] + card->tst_size - 2) &&
	      (addr < card->tst[0] + card->tst_size)) ||
	     ((addr > card->tst[1] + card->tst_size - 2) &&
	      (addr < card->tst[1] + card->tst_size)))) {
		printk("%s: ERROR: TST JMP section at %08lx written: %08x\n",
		       card->name, addr, value);
	}

	spin_lock_irqsave(&card->cmd_lock, flags);
	writel(value, SAR_REG_DR0);
	writel(SAR_CMD_WRITE_SRAM | (addr << 2), SAR_REG_CMD);
	waitfor_idle(card);
	spin_unlock_irqrestore(&card->cmd_lock, flags);
}

static u8
read_utility(void *dev, unsigned long ubus_addr)
{
	struct idt77252_dev *card = dev;
	unsigned long flags;
	u8 value;

	if (!card) {
		printk("Error: No such device.\n");
		return -1;
	}

	spin_lock_irqsave(&card->cmd_lock, flags);
	writel(SAR_CMD_READ_UTILITY + ubus_addr, SAR_REG_CMD);
	waitfor_idle(card);
	value = readl(SAR_REG_DR0);
	spin_unlock_irqrestore(&card->cmd_lock, flags);
	return value;
}

static void
write_utility(void *dev, unsigned long ubus_addr, u8 value)
{
	struct idt77252_dev *card = dev;
	unsigned long flags;

	if (!card) {
		printk("Error: No such device.\n");
		return;
	}

	spin_lock_irqsave(&card->cmd_lock, flags);
	writel((u32) value, SAR_REG_DR0);
	writel(SAR_CMD_WRITE_UTILITY + ubus_addr, SAR_REG_CMD);
	waitfor_idle(card);
	spin_unlock_irqrestore(&card->cmd_lock, flags);
}

#ifdef HAVE_EEPROM
static u32 rdsrtab[] =
{
	SAR_GP_EECS | SAR_GP_EESCLK,
	0,
	SAR_GP_EESCLK,			/* 0 */
	0,
	SAR_GP_EESCLK,			/* 0 */
	0,
	SAR_GP_EESCLK,			/* 0 */
	0,
	SAR_GP_EESCLK,			/* 0 */
	0,
	SAR_GP_EESCLK,			/* 0 */
	SAR_GP_EEDO,
	SAR_GP_EESCLK | SAR_GP_EEDO,	/* 1 */
	0,
	SAR_GP_EESCLK,			/* 0 */
	SAR_GP_EEDO,
	SAR_GP_EESCLK | SAR_GP_EEDO	/* 1 */
};

static u32 wrentab[] =
{
	SAR_GP_EECS | SAR_GP_EESCLK,
	0,
	SAR_GP_EESCLK,			/* 0 */
	0,
	SAR_GP_EESCLK,			/* 0 */
	0,
	SAR_GP_EESCLK,			/* 0 */
	0,
	SAR_GP_EESCLK,			/* 0 */
	SAR_GP_EEDO,
	SAR_GP_EESCLK | SAR_GP_EEDO,	/* 1 */
	SAR_GP_EEDO,
	SAR_GP_EESCLK | SAR_GP_EEDO,	/* 1 */
	0,
	SAR_GP_EESCLK,			/* 0 */
	0,
	SAR_GP_EESCLK			/* 0 */
};

static u32 rdtab[] =
{
	SAR_GP_EECS | SAR_GP_EESCLK,
	0,
	SAR_GP_EESCLK,			/* 0 */
	0,
	SAR_GP_EESCLK,			/* 0 */
	0,
	SAR_GP_EESCLK,			/* 0 */
	0,
	SAR_GP_EESCLK,			/* 0 */
	0,
	SAR_GP_EESCLK,			/* 0 */
	0,
	SAR_GP_EESCLK,			/* 0 */
	SAR_GP_EEDO,
	SAR_GP_EESCLK | SAR_GP_EEDO,	/* 1 */
	SAR_GP_EEDO,
	SAR_GP_EESCLK | SAR_GP_EEDO	/* 1 */
};

static u32 wrtab[] =
{
	SAR_GP_EECS | SAR_GP_EESCLK,
	0,
	SAR_GP_EESCLK,			/* 0 */
	0,
	SAR_GP_EESCLK,			/* 0 */
	0,
	SAR_GP_EESCLK,			/* 0 */
	0,
	SAR_GP_EESCLK,			/* 0 */
	0,
	SAR_GP_EESCLK,			/* 0 */
	0,
	SAR_GP_EESCLK,			/* 0 */
	SAR_GP_EEDO,
	SAR_GP_EESCLK | SAR_GP_EEDO,	/* 1 */
	0,
	SAR_GP_EESCLK			/* 0 */
};

static u32 clktab[] =
{
	0,
	SAR_GP_EESCLK,
	0,
	SAR_GP_EESCLK,
	0,
	SAR_GP_EESCLK,
	0,
	SAR_GP_EESCLK,
	0,
	SAR_GP_EESCLK,
	0,
	SAR_GP_EESCLK,
	0,
	SAR_GP_EESCLK,
	0,
	SAR_GP_EESCLK,
	0
};

static u32
idt77252_read_gp(struct idt77252_dev *card)
{
	u32 gp;

	gp = readl(SAR_REG_GP);
#if 0
	printk("RD: %s\n", gp & SAR_GP_EEDI ? "1" : "0");
#endif
	return gp;
}

static void
idt77252_write_gp(struct idt77252_dev *card, u32 value)
{
	unsigned long flags;

#if 0
	printk("WR: %s %s %s\n", value & SAR_GP_EECS ? "   " : "/CS",
	       value & SAR_GP_EESCLK ? "HIGH" : "LOW ",
	       value & SAR_GP_EEDO   ? "1" : "0");
#endif

	spin_lock_irqsave(&card->cmd_lock, flags);
	waitfor_idle(card);
	writel(value, SAR_REG_GP);
	spin_unlock_irqrestore(&card->cmd_lock, flags);
}

static u8
idt77252_eeprom_read_status(struct idt77252_dev *card)
{
	u8 byte;
	u32 gp;
	int i, j;

	gp = idt77252_read_gp(card) & ~(SAR_GP_EESCLK|SAR_GP_EECS|SAR_GP_EEDO);

	for (i = 0; i < ARRAY_SIZE(rdsrtab); i++) {
		idt77252_write_gp(card, gp | rdsrtab[i]);
		udelay(5);
	}
	idt77252_write_gp(card, gp | SAR_GP_EECS);
	udelay(5);

	byte = 0;
	for (i = 0, j = 0; i < 8; i++) {
		byte <<= 1;

		idt77252_write_gp(card, gp | clktab[j++]);
		udelay(5);

		byte |= idt77252_read_gp(card) & SAR_GP_EEDI ? 1 : 0;

		idt77252_write_gp(card, gp | clktab[j++]);
		udelay(5);
	}
	idt77252_write_gp(card, gp | SAR_GP_EECS);
	udelay(5);

	return byte;
}

static u8
idt77252_eeprom_read_byte(struct idt77252_dev *card, u8 offset)
{
	u8 byte;
	u32 gp;
	int i, j;

	gp = idt77252_read_gp(card) & ~(SAR_GP_EESCLK|SAR_GP_EECS|SAR_GP_EEDO);

	for (i = 0; i < ARRAY_SIZE(rdtab); i++) {
		idt77252_write_gp(card, gp | rdtab[i]);
		udelay(5);
	}
	idt77252_write_gp(card, gp | SAR_GP_EECS);
	udelay(5);

	for (i = 0, j = 0; i < 8; i++) {
		idt77252_write_gp(card, gp | clktab[j++] |
					(offset & 1 ? SAR_GP_EEDO : 0));
		udelay(5);

		idt77252_write_gp(card, gp | clktab[j++] |
					(offset & 1 ? SAR_GP_EEDO : 0));
		udelay(5);

		offset >>= 1;
	}
	idt77252_write_gp(card, gp | SAR_GP_EECS);
	udelay(5);

	byte = 0;
	for (i = 0, j = 0; i < 8; i++) {
		byte <<= 1;

		idt77252_write_gp(card, gp | clktab[j++]);
		udelay(5);

		byte |= idt77252_read_gp(card) & SAR_GP_EEDI ? 1 : 0;

		idt77252_write_gp(card, gp | clktab[j++]);
		udelay(5);
	}
	idt77252_write_gp(card, gp | SAR_GP_EECS);
	udelay(5);

	return byte;
}

static void
idt77252_eeprom_write_byte(struct idt77252_dev *card, u8 offset, u8 data)
{
	u32 gp;
	int i, j;

	gp = idt77252_read_gp(card) & ~(SAR_GP_EESCLK|SAR_GP_EECS|SAR_GP_EEDO);

	for (i = 0; i < ARRAY_SIZE(wrentab); i++) {
		idt77252_write_gp(card, gp | wrentab[i]);
		udelay(5);
	}
	idt77252_write_gp(card, gp | SAR_GP_EECS);
	udelay(5);

	for (i = 0; i < ARRAY_SIZE(wrtab); i++) {
		idt77252_write_gp(card, gp | wrtab[i]);
		udelay(5);
	}
	idt77252_write_gp(card, gp | SAR_GP_EECS);
	udelay(5);

	for (i = 0, j = 0; i < 8; i++) {
		idt77252_write_gp(card, gp | clktab[j++] |
					(offset & 1 ? SAR_GP_EEDO : 0));
		udelay(5);

		idt77252_write_gp(card, gp | clktab[j++] |
					(offset & 1 ? SAR_GP_EEDO : 0));
		udelay(5);

		offset >>= 1;
	}
	idt77252_write_gp(card, gp | SAR_GP_EECS);
	udelay(5);

	for (i = 0, j = 0; i < 8; i++) {
		idt77252_write_gp(card, gp | clktab[j++] |
					(data & 1 ? SAR_GP_EEDO : 0));
		udelay(5);

		idt77252_write_gp(card, gp | clktab[j++] |
					(data & 1 ? SAR_GP_EEDO : 0));
		udelay(5);

		data >>= 1;
	}
	idt77252_write_gp(card, gp | SAR_GP_EECS);
	udelay(5);
}

static void
idt77252_eeprom_init(struct idt77252_dev *card)
{
	u32 gp;

	gp = idt77252_read_gp(card) & ~(SAR_GP_EESCLK|SAR_GP_EECS|SAR_GP_EEDO);

	idt77252_write_gp(card, gp | SAR_GP_EECS | SAR_GP_EESCLK);
	udelay(5);
	idt77252_write_gp(card, gp | SAR_GP_EECS);
	udelay(5);
	idt77252_write_gp(card, gp | SAR_GP_EECS | SAR_GP_EESCLK);
	udelay(5);
	idt77252_write_gp(card, gp | SAR_GP_EECS);
	udelay(5);
}
#endif /* HAVE_EEPROM */


#ifdef CONFIG_ATM_IDT77252_DEBUG
static void
dump_tct(struct idt77252_dev *card, int index)
{
	unsigned long tct;
	int i;

	tct = (unsigned long) (card->tct_base + index * SAR_SRAM_TCT_SIZE);

	printk("%s: TCT %x:", card->name, index);
	for (i = 0; i < 8; i++) {
		printk(" %08x", read_sram(card, tct + i));
	}
	printk("\n");
}

static void
idt77252_tx_dump(struct idt77252_dev *card)
{
	struct atm_vcc *vcc;
	struct vc_map *vc;
	int i;

	printk("%s\n", __func__);
	for (i = 0; i < card->tct_size; i++) {
		vc = card->vcs[i];
		if (!vc)
			continue;

		vcc = NULL;
		if (vc->rx_vcc)
			vcc = vc->rx_vcc;
		else if (vc->tx_vcc)
			vcc = vc->tx_vcc;

		if (!vcc)
			continue;

		printk("%s: Connection %d:\n", card->name, vc->index);
		dump_tct(card, vc->index);
	}
}
#endif


/*****************************************************************************/
/*                                                                           */
/* SCQ Handling                                                              */
/*                                                                           */
/*****************************************************************************/

static int
sb_pool_add(struct idt77252_dev *card, struct sk_buff *skb, int queue)
{
	struct sb_pool *pool = &card->sbpool[queue];
	int index;

	index = pool->index;
	while (pool->skb[index]) {
		index = (index + 1) & FBQ_MASK;
		if (index == pool->index)
			return -ENOBUFS;
	}

	pool->skb[index] = skb;
	IDT77252_PRV_POOL(skb) = POOL_HANDLE(queue, index);

	pool->index = (index + 1) & FBQ_MASK;
	return 0;
}

static void
sb_pool_remove(struct idt77252_dev *card, struct sk_buff *skb)
{
	unsigned int queue, index;
	u32 handle;

	handle = IDT77252_PRV_POOL(skb);

	queue = POOL_QUEUE(handle);
	if (queue > 3)
		return;

	index = POOL_INDEX(handle);
	if (index > FBQ_SIZE - 1)
		return;

	card->sbpool[queue].skb[index] = NULL;
}

static struct sk_buff *
sb_pool_skb(struct idt77252_dev *card, u32 handle)
{
	unsigned int queue, index;

	queue = POOL_QUEUE(handle);
	if (queue > 3)
		return NULL;

	index = POOL_INDEX(handle);
	if (index > FBQ_SIZE - 1)
		return NULL;

	return card->sbpool[queue].skb[index];
}

static struct scq_info *
alloc_scq(struct idt77252_dev *card, int class)
{
	struct scq_info *scq;

	scq = kzalloc(sizeof(struct scq_info), GFP_KERNEL);
	if (!scq)
		return NULL;
	scq->base = pci_alloc_consistent(card->pcidev, SCQ_SIZE,
					 &scq->paddr);
	if (scq->base == NULL) {
		kfree(scq);
		return NULL;
	}
	memset(scq->base, 0, SCQ_SIZE);

	scq->next = scq->base;
	scq->last = scq->base + (SCQ_ENTRIES - 1);
	atomic_set(&scq->used, 0);

	spin_lock_init(&scq->lock);
	spin_lock_init(&scq->skblock);

	skb_queue_head_init(&scq->transmit);
	skb_queue_head_init(&scq->pending);

	TXPRINTK("idt77252: SCQ: base 0x%p, next 0x%p, last 0x%p, paddr %08llx\n",
		 scq->base, scq->next, scq->last, (unsigned long long)scq->paddr);

	return scq;
}

static void
free_scq(struct idt77252_dev *card, struct scq_info *scq)
{
	struct sk_buff *skb;
	struct atm_vcc *vcc;

	pci_free_consistent(card->pcidev, SCQ_SIZE,
			    scq->base, scq->paddr);

	while ((skb = skb_dequeue(&scq->transmit))) {
		pci_unmap_single(card->pcidev, IDT77252_PRV_PADDR(skb),
				 skb->len, PCI_DMA_TODEVICE);

		vcc = ATM_SKB(skb)->vcc;
		if (vcc->pop)
			vcc->pop(vcc, skb);
		else
			dev_kfree_skb(skb);
	}

	while ((skb = skb_dequeue(&scq->pending))) {
		pci_unmap_single(card->pcidev, IDT77252_PRV_PADDR(skb),
				 skb->len, PCI_DMA_TODEVICE);

		vcc = ATM_SKB(skb)->vcc;
		if (vcc->pop)
			vcc->pop(vcc, skb);
		else
			dev_kfree_skb(skb);
	}

	kfree(scq);
}


static int
push_on_scq(struct idt77252_dev *card, struct vc_map *vc, struct sk_buff *skb)
{
	struct scq_info *scq = vc->scq;
	unsigned long flags;
	struct scqe *tbd;
	int entries;

	TXPRINTK("%s: SCQ: next 0x%p\n", card->name, scq->next);

	atomic_inc(&scq->used);
	entries = atomic_read(&scq->used);
	if (entries > (SCQ_ENTRIES - 1)) {
		atomic_dec(&scq->used);
		goto out;
	}

	skb_queue_tail(&scq->transmit, skb);

	spin_lock_irqsave(&vc->lock, flags);
	if (vc->estimator) {
		struct atm_vcc *vcc = vc->tx_vcc;
		struct sock *sk = sk_atm(vcc);

		vc->estimator->cells += (skb->len + 47) / 48;
		if (atomic_read(&sk->sk_wmem_alloc) >
		    (sk->sk_sndbuf >> 1)) {
			u32 cps = vc->estimator->maxcps;

			vc->estimator->cps = cps;
			vc->estimator->avcps = cps << 5;
			if (vc->lacr < vc->init_er) {
				vc->lacr = vc->init_er;
				writel(TCMDQ_LACR | (vc->lacr << 16) |
				       vc->index, SAR_REG_TCMDQ);
			}
		}
	}
	spin_unlock_irqrestore(&vc->lock, flags);

	tbd = &IDT77252_PRV_TBD(skb);

	spin_lock_irqsave(&scq->lock, flags);
	scq->next->word_1 = cpu_to_le32(tbd->word_1 |
					SAR_TBD_TSIF | SAR_TBD_GTSI);
	scq->next->word_2 = cpu_to_le32(tbd->word_2);
	scq->next->word_3 = cpu_to_le32(tbd->word_3);
	scq->next->word_4 = cpu_to_le32(tbd->word_4);

	if (scq->next == scq->last)
		scq->next = scq->base;
	else
		scq->next++;

	write_sram(card, scq->scd,
		   scq->paddr +
		   (u32)((unsigned long)scq->next - (unsigned long)scq->base));
	spin_unlock_irqrestore(&scq->lock, flags);

	scq->trans_start = jiffies;

	if (test_and_clear_bit(VCF_IDLE, &vc->flags)) {
		writel(TCMDQ_START_LACR | (vc->lacr << 16) | vc->index,
		       SAR_REG_TCMDQ);
	}

	TXPRINTK("%d entries in SCQ used (push).\n", atomic_read(&scq->used));

	XPRINTK("%s: SCQ (after push %2d) head = 0x%x, next = 0x%p.\n",
		card->name, atomic_read(&scq->used),
		read_sram(card, scq->scd + 1), scq->next);

	return 0;

out:
	if (time_after(jiffies, scq->trans_start + HZ)) {
		printk("%s: Error pushing TBD for %d.%d\n",
		       card->name, vc->tx_vcc->vpi, vc->tx_vcc->vci);
#ifdef CONFIG_ATM_IDT77252_DEBUG
		idt77252_tx_dump(card);
#endif
		scq->trans_start = jiffies;
	}

	return -ENOBUFS;
}


static void
drain_scq(struct idt77252_dev *card, struct vc_map *vc)
{
	struct scq_info *scq = vc->scq;
	struct sk_buff *skb;
	struct atm_vcc *vcc;

	TXPRINTK("%s: SCQ (before drain %2d) next = 0x%p.\n",
		 card->name, atomic_read(&scq->used), scq->next);

	skb = skb_dequeue(&scq->transmit);
	if (skb) {
		TXPRINTK("%s: freeing skb at %p.\n", card->name, skb);

		pci_unmap_single(card->pcidev, IDT77252_PRV_PADDR(skb),
				 skb->len, PCI_DMA_TODEVICE);

		vcc = ATM_SKB(skb)->vcc;

		if (vcc->pop)
			vcc->pop(vcc, skb);
		else
			dev_kfree_skb(skb);

		atomic_inc(&vcc->stats->tx);
	}

	atomic_dec(&scq->used);

	spin_lock(&scq->skblock);
	while ((skb = skb_dequeue(&scq->pending))) {
		if (push_on_scq(card, vc, skb)) {
			skb_queue_head(&vc->scq->pending, skb);
			break;
		}
	}
	spin_unlock(&scq->skblock);
}

static int
queue_skb(struct idt77252_dev *card, struct vc_map *vc,
	  struct sk_buff *skb, int oam)
{
	struct atm_vcc *vcc;
	struct scqe *tbd;
	unsigned long flags;
	int error;
	int aal;

	if (skb->len == 0) {
		printk("%s: invalid skb->len (%d)\n", card->name, skb->len);
		return -EINVAL;
	}

	TXPRINTK("%s: Sending %d bytes of data.\n",
		 card->name, skb->len);

	tbd = &IDT77252_PRV_TBD(skb);
	vcc = ATM_SKB(skb)->vcc;

	IDT77252_PRV_PADDR(skb) = pci_map_single(card->pcidev, skb->data,
						 skb->len, PCI_DMA_TODEVICE);

	error = -EINVAL;

	if (oam) {
		if (skb->len != 52)
			goto errout;

		tbd->word_1 = SAR_TBD_OAM | ATM_CELL_PAYLOAD | SAR_TBD_EPDU;
		tbd->word_2 = IDT77252_PRV_PADDR(skb) + 4;
		tbd->word_3 = 0x00000000;
		tbd->word_4 = (skb->data[0] << 24) | (skb->data[1] << 16) |
			      (skb->data[2] <<  8) | (skb->data[3] <<  0);

		if (test_bit(VCF_RSV, &vc->flags))
			vc = card->vcs[0];

		goto done;
	}

	if (test_bit(VCF_RSV, &vc->flags)) {
		printk("%s: Trying to transmit on reserved VC\n", card->name);
		goto errout;
	}

	aal = vcc->qos.aal;

	switch (aal) {
	case ATM_AAL0:
	case ATM_AAL34:
		if (skb->len > 52)
			goto errout;

		if (aal == ATM_AAL0)
			tbd->word_1 = SAR_TBD_EPDU | SAR_TBD_AAL0 |
				      ATM_CELL_PAYLOAD;
		else
			tbd->word_1 = SAR_TBD_EPDU | SAR_TBD_AAL34 |
				      ATM_CELL_PAYLOAD;

		tbd->word_2 = IDT77252_PRV_PADDR(skb) + 4;
		tbd->word_3 = 0x00000000;
		tbd->word_4 = (skb->data[0] << 24) | (skb->data[1] << 16) |
			      (skb->data[2] <<  8) | (skb->data[3] <<  0);
		break;

	case ATM_AAL5:
		tbd->word_1 = SAR_TBD_EPDU | SAR_TBD_AAL5 | skb->len;
		tbd->word_2 = IDT77252_PRV_PADDR(skb);
		tbd->word_3 = skb->len;
		tbd->word_4 = (vcc->vpi << SAR_TBD_VPI_SHIFT) |
			      (vcc->vci << SAR_TBD_VCI_SHIFT);
		break;

	case ATM_AAL1:
	case ATM_AAL2:
	default:
		printk("%s: Traffic type not supported.\n", card->name);
		error = -EPROTONOSUPPORT;
		goto errout;
	}

done:
	spin_lock_irqsave(&vc->scq->skblock, flags);
	skb_queue_tail(&vc->scq->pending, skb);

	while ((skb = skb_dequeue(&vc->scq->pending))) {
		if (push_on_scq(card, vc, skb)) {
			skb_queue_head(&vc->scq->pending, skb);
			break;
		}
	}
	spin_unlock_irqrestore(&vc->scq->skblock, flags);

	return 0;

errout:
	pci_unmap_single(card->pcidev, IDT77252_PRV_PADDR(skb),
			 skb->len, PCI_DMA_TODEVICE);
	return error;
}

static unsigned long
get_free_scd(struct idt77252_dev *card, struct vc_map *vc)
{
	int i;

	for (i = 0; i < card->scd_size; i++) {
		if (!card->scd2vc[i]) {
			card->scd2vc[i] = vc;
			vc->scd_index = i;
			return card->scd_base + i * SAR_SRAM_SCD_SIZE;
		}
	}
	return 0;
}

static void
fill_scd(struct idt77252_dev *card, struct scq_info *scq, int class)
{
	write_sram(card, scq->scd, scq->paddr);
	write_sram(card, scq->scd + 1, 0x00000000);
	write_sram(card, scq->scd + 2, 0xffffffff);
	write_sram(card, scq->scd + 3, 0x00000000);
}

static void
clear_scd(struct idt77252_dev *card, struct scq_info *scq, int class)
{
	return;
}

/*****************************************************************************/
/*                                                                           */
/* RSQ Handling                                                              */
/*                                                                           */
/*****************************************************************************/

static int
init_rsq(struct idt77252_dev *card)
{
	struct rsq_entry *rsqe;

	card->rsq.base = pci_alloc_consistent(card->pcidev, RSQSIZE,
					      &card->rsq.paddr);
	if (card->rsq.base == NULL) {
		printk("%s: can't allocate RSQ.\n", card->name);
		return -1;
	}
	memset(card->rsq.base, 0, RSQSIZE);

	card->rsq.last = card->rsq.base + RSQ_NUM_ENTRIES - 1;
	card->rsq.next = card->rsq.last;
	for (rsqe = card->rsq.base; rsqe <= card->rsq.last; rsqe++)
		rsqe->word_4 = 0;

	writel((unsigned long) card->rsq.last - (unsigned long) card->rsq.base,
	       SAR_REG_RSQH);
	writel(card->rsq.paddr, SAR_REG_RSQB);

	IPRINTK("%s: RSQ base at 0x%lx (0x%x).\n", card->name,
		(unsigned long) card->rsq.base,
		readl(SAR_REG_RSQB));
	IPRINTK("%s: RSQ head = 0x%x, base = 0x%x, tail = 0x%x.\n",
		card->name,
		readl(SAR_REG_RSQH),
		readl(SAR_REG_RSQB),
		readl(SAR_REG_RSQT));

	return 0;
}

static void
deinit_rsq(struct idt77252_dev *card)
{
	pci_free_consistent(card->pcidev, RSQSIZE,
			    card->rsq.base, card->rsq.paddr);
}

static void
dequeue_rx(struct idt77252_dev *card, struct rsq_entry *rsqe)
{
	struct atm_vcc *vcc;
	struct sk_buff *skb;
	struct rx_pool *rpp;
	struct vc_map *vc;
	u32 header, vpi, vci;
	u32 stat;
	int i;

	stat = le32_to_cpu(rsqe->word_4);

	if (stat & SAR_RSQE_IDLE) {
		RXPRINTK("%s: message about inactive connection.\n",
			 card->name);
		return;
	}

	skb = sb_pool_skb(card, le32_to_cpu(rsqe->word_2));
	if (skb == NULL) {
		printk("%s: NULL skb in %s, rsqe: %08x %08x %08x %08x\n",
		       card->name, __func__,
		       le32_to_cpu(rsqe->word_1), le32_to_cpu(rsqe->word_2),
		       le32_to_cpu(rsqe->word_3), le32_to_cpu(rsqe->word_4));
		return;
	}

	header = le32_to_cpu(rsqe->word_1);
	vpi = (header >> 16) & 0x00ff;
	vci = (header >>  0) & 0xffff;

	RXPRINTK("%s: SDU for %d.%d received in buffer 0x%p (data 0x%p).\n",
		 card->name, vpi, vci, skb, skb->data);

	if ((vpi >= (1 << card->vpibits)) || (vci != (vci & card->vcimask))) {
		printk("%s: SDU received for out-of-range vc %u.%u\n",
		       card->name, vpi, vci);
		recycle_rx_skb(card, skb);
		return;
	}

	vc = card->vcs[VPCI2VC(card, vpi, vci)];
	if (!vc || !test_bit(VCF_RX, &vc->flags)) {
		printk("%s: SDU received on non RX vc %u.%u\n",
		       card->name, vpi, vci);
		recycle_rx_skb(card, skb);
		return;
	}

	vcc = vc->rx_vcc;

	pci_dma_sync_single_for_cpu(card->pcidev, IDT77252_PRV_PADDR(skb),
				    skb_end_pointer(skb) - skb->data,
				    PCI_DMA_FROMDEVICE);

	if ((vcc->qos.aal == ATM_AAL0) ||
	    (vcc->qos.aal == ATM_AAL34)) {
		struct sk_buff *sb;
		unsigned char *cell;
		u32 aal0;

		cell = skb->data;
		for (i = (stat & SAR_RSQE_CELLCNT); i; i--) {
			if ((sb = dev_alloc_skb(64)) == NULL) {
				printk("%s: Can't allocate buffers for aal0.\n",
				       card->name);
				atomic_add(i, &vcc->stats->rx_drop);
				break;
			}
			if (!atm_charge(vcc, sb->truesize)) {
				RXPRINTK("%s: atm_charge() dropped aal0 packets.\n",
					 card->name);
				atomic_add(i - 1, &vcc->stats->rx_drop);
				dev_kfree_skb(sb);
				break;
			}
			aal0 = (vpi << ATM_HDR_VPI_SHIFT) |
			       (vci << ATM_HDR_VCI_SHIFT);
			aal0 |= (stat & SAR_RSQE_EPDU) ? 0x00000002 : 0;
			aal0 |= (stat & SAR_RSQE_CLP)  ? 0x00000001 : 0;

			*((u32 *) sb->data) = aal0;
			skb_put(sb, sizeof(u32));
			memcpy(skb_put(sb, ATM_CELL_PAYLOAD),
			       cell, ATM_CELL_PAYLOAD);

			ATM_SKB(sb)->vcc = vcc;
			__net_timestamp(sb);
			vcc->push(vcc, sb);
			atomic_inc(&vcc->stats->rx);

			cell += ATM_CELL_PAYLOAD;
		}

		recycle_rx_skb(card, skb);
		return;
	}
	if (vcc->qos.aal != ATM_AAL5) {
		printk("%s: Unexpected AAL type in dequeue_rx(): %d.\n",
		       card->name, vcc->qos.aal);
		recycle_rx_skb(card, skb);
		return;
	}
	skb->len = (stat & SAR_RSQE_CELLCNT) * ATM_CELL_PAYLOAD;

	rpp = &vc->rcv.rx_pool;

	__skb_queue_tail(&rpp->queue, skb);
	rpp->len += skb->len;

	if (stat & SAR_RSQE_EPDU) {
		unsigned char *l1l2;
		unsigned int len;

		l1l2 = (unsigned char *) ((unsigned long) skb->data + skb->len - 6);

		len = (l1l2[0] << 8) | l1l2[1];
		len = len ? len : 0x10000;

		RXPRINTK("%s: PDU has %d bytes.\n", card->name, len);

		if ((len + 8 > rpp->len) || (len + (47 + 8) < rpp->len)) {
			RXPRINTK("%s: AAL5 PDU size mismatch: %d != %d. "
			         "(CDC: %08x)\n",
			         card->name, len, rpp->len, readl(SAR_REG_CDC));
			recycle_rx_pool_skb(card, rpp);
			atomic_inc(&vcc->stats->rx_err);
			return;
		}
		if (stat & SAR_RSQE_CRC) {
			RXPRINTK("%s: AAL5 CRC error.\n", card->name);
			recycle_rx_pool_skb(card, rpp);
			atomic_inc(&vcc->stats->rx_err);
			return;
		}
		if (skb_queue_len(&rpp->queue) > 1) {
			struct sk_buff *sb;

			skb = dev_alloc_skb(rpp->len);
			if (!skb) {
				RXPRINTK("%s: Can't alloc RX skb.\n",
					 card->name);
				recycle_rx_pool_skb(card, rpp);
				atomic_inc(&vcc->stats->rx_err);
				return;
			}
			if (!atm_charge(vcc, skb->truesize)) {
				recycle_rx_pool_skb(card, rpp);
				dev_kfree_skb(skb);
				return;
			}
			skb_queue_walk(&rpp->queue, sb)
				memcpy(skb_put(skb, sb->len),
				       sb->data, sb->len);

			recycle_rx_pool_skb(card, rpp);

			skb_trim(skb, len);
			ATM_SKB(skb)->vcc = vcc;
			__net_timestamp(skb);

			vcc->push(vcc, skb);
			atomic_inc(&vcc->stats->rx);

			return;
		}

		flush_rx_pool(card, rpp);

		if (!atm_charge(vcc, skb->truesize)) {
			recycle_rx_skb(card, skb);
			return;
		}

		pci_unmap_single(card->pcidev, IDT77252_PRV_PADDR(skb),
				 skb_end_pointer(skb) - skb->data,
				 PCI_DMA_FROMDEVICE);
		sb_pool_remove(card, skb);

		skb_trim(skb, len);
		ATM_SKB(skb)->vcc = vcc;
		__net_timestamp(skb);

		vcc->push(vcc, skb);
		atomic_inc(&vcc->stats->rx);

		if (skb->truesize > SAR_FB_SIZE_3)
			add_rx_skb(card, 3, SAR_FB_SIZE_3, 1);
		else if (skb->truesize > SAR_FB_SIZE_2)
			add_rx_skb(card, 2, SAR_FB_SIZE_2, 1);
		else if (skb->truesize > SAR_FB_SIZE_1)
			add_rx_skb(card, 1, SAR_FB_SIZE_1, 1);
		else
			add_rx_skb(card, 0, SAR_FB_SIZE_0, 1);
		return;
	}
}

static void
idt77252_rx(struct idt77252_dev *card)
{
	struct rsq_entry *rsqe;

	if (card->rsq.next == card->rsq.last)
		rsqe = card->rsq.base;
	else
		rsqe = card->rsq.next + 1;

	if (!(le32_to_cpu(rsqe->word_4) & SAR_RSQE_VALID)) {
		RXPRINTK("%s: no entry in RSQ.\n", card->name);
		return;
	}

	do {
		dequeue_rx(card, rsqe);
		rsqe->word_4 = 0;
		card->rsq.next = rsqe;
		if (card->rsq.next == card->rsq.last)
			rsqe = card->rsq.base;
		else
			rsqe = card->rsq.next + 1;
	} while (le32_to_cpu(rsqe->word_4) & SAR_RSQE_VALID);

	writel((unsigned long) card->rsq.next - (unsigned long) card->rsq.base,
	       SAR_REG_RSQH);
}

static void
idt77252_rx_raw(struct idt77252_dev *card)
{
	struct sk_buff	*queue;
	u32		head, tail;
	struct atm_vcc	*vcc;
	struct vc_map	*vc;
	struct sk_buff	*sb;

	if (card->raw_cell_head == NULL) {
		u32 handle = le32_to_cpu(*(card->raw_cell_hnd + 1));
		card->raw_cell_head = sb_pool_skb(card, handle);
	}

	queue = card->raw_cell_head;
	if (!queue)
		return;

	head = IDT77252_PRV_PADDR(queue) + (queue->data - queue->head - 16);
	tail = readl(SAR_REG_RAWCT);

	pci_dma_sync_single_for_cpu(card->pcidev, IDT77252_PRV_PADDR(queue),
				    skb_end_pointer(queue) - queue->head - 16,
				    PCI_DMA_FROMDEVICE);

	while (head != tail) {
		unsigned int vpi, vci, pti;
		u32 header;

		header = le32_to_cpu(*(u32 *) &queue->data[0]);

		vpi = (header & ATM_HDR_VPI_MASK) >> ATM_HDR_VPI_SHIFT;
		vci = (header & ATM_HDR_VCI_MASK) >> ATM_HDR_VCI_SHIFT;
		pti = (header & ATM_HDR_PTI_MASK) >> ATM_HDR_PTI_SHIFT;

#ifdef CONFIG_ATM_IDT77252_DEBUG
		if (debug & DBG_RAW_CELL) {
			int i;

			printk("%s: raw cell %x.%02x.%04x.%x.%x\n",
			       card->name, (header >> 28) & 0x000f,
			       (header >> 20) & 0x00ff,
			       (header >>  4) & 0xffff,
			       (header >>  1) & 0x0007,
			       (header >>  0) & 0x0001);
			for (i = 16; i < 64; i++)
				printk(" %02x", queue->data[i]);
			printk("\n");
		}
#endif

		if (vpi >= (1<<card->vpibits) || vci >= (1<<card->vcibits)) {
			RPRINTK("%s: SDU received for out-of-range vc %u.%u\n",
				card->name, vpi, vci);
			goto drop;
		}

		vc = card->vcs[VPCI2VC(card, vpi, vci)];
		if (!vc || !test_bit(VCF_RX, &vc->flags)) {
			RPRINTK("%s: SDU received on non RX vc %u.%u\n",
				card->name, vpi, vci);
			goto drop;
		}

		vcc = vc->rx_vcc;

		if (vcc->qos.aal != ATM_AAL0) {
			RPRINTK("%s: raw cell for non AAL0 vc %u.%u\n",
				card->name, vpi, vci);
			atomic_inc(&vcc->stats->rx_drop);
			goto drop;
		}
	
		if ((sb = dev_alloc_skb(64)) == NULL) {
			printk("%s: Can't allocate buffers for AAL0.\n",
			       card->name);
			atomic_inc(&vcc->stats->rx_err);
			goto drop;
		}

		if (!atm_charge(vcc, sb->truesize)) {
			RXPRINTK("%s: atm_charge() dropped AAL0 packets.\n",
				 card->name);
			dev_kfree_skb(sb);
			goto drop;
		}

		*((u32 *) sb->data) = header;
		skb_put(sb, sizeof(u32));
		memcpy(skb_put(sb, ATM_CELL_PAYLOAD), &(queue->data[16]),
		       ATM_CELL_PAYLOAD);

		ATM_SKB(sb)->vcc = vcc;
		__net_timestamp(sb);
		vcc->push(vcc, sb);
		atomic_inc(&vcc->stats->rx);

drop:
		skb_pull(queue, 64);

		head = IDT77252_PRV_PADDR(queue)
					+ (queue->data - queue->head - 16);

		if (queue->len < 128) {
			struct sk_buff *next;
			u32 handle;

			head = le32_to_cpu(*(u32 *) &queue->data[0]);
			handle = le32_to_cpu(*(u32 *) &queue->data[4]);

			next = sb_pool_skb(card, handle);
			recycle_rx_skb(card, queue);

			if (next) {
				card->raw_cell_head = next;
				queue = card->raw_cell_head;
				pci_dma_sync_single_for_cpu(card->pcidev,
							    IDT77252_PRV_PADDR(queue),
							    (skb_end_pointer(queue) -
							     queue->data),
							    PCI_DMA_FROMDEVICE);
			} else {
				card->raw_cell_head = NULL;
				printk("%s: raw cell queue overrun\n",
				       card->name);
				break;
			}
		}
	}
}


/*****************************************************************************/
/*                                                                           */
/* TSQ Handling                                                              */
/*                                                                           */
/*****************************************************************************/

static int
init_tsq(struct idt77252_dev *card)
{
	struct tsq_entry *tsqe;

	card->tsq.base = pci_alloc_consistent(card->pcidev, RSQSIZE,
					      &card->tsq.paddr);
	if (card->tsq.base == NULL) {
		printk("%s: can't allocate TSQ.\n", card->name);
		return -1;
	}
	memset(card->tsq.base, 0, TSQSIZE);

	card->tsq.last = card->tsq.base + TSQ_NUM_ENTRIES - 1;
	card->tsq.next = card->tsq.last;
	for (tsqe = card->tsq.base; tsqe <= card->tsq.last; tsqe++)
		tsqe->word_2 = cpu_to_le32(SAR_TSQE_INVALID);

	writel(card->tsq.paddr, SAR_REG_TSQB);
	writel((unsigned long) card->tsq.next - (unsigned long) card->tsq.base,
	       SAR_REG_TSQH);

	return 0;
}

static void
deinit_tsq(struct idt77252_dev *card)
{
	pci_free_consistent(card->pcidev, TSQSIZE,
			    card->tsq.base, card->tsq.paddr);
}

static void
idt77252_tx(struct idt77252_dev *card)
{
	struct tsq_entry *tsqe;
	unsigned int vpi, vci;
	struct vc_map *vc;
	u32 conn, stat;

	if (card->tsq.next == card->tsq.last)
		tsqe = card->tsq.base;
	else
		tsqe = card->tsq.next + 1;

	TXPRINTK("idt77252_tx: tsq  %p: base %p, next %p, last %p\n", tsqe,
		 card->tsq.base, card->tsq.next, card->tsq.last);
	TXPRINTK("idt77252_tx: tsqb %08x, tsqt %08x, tsqh %08x, \n",
		 readl(SAR_REG_TSQB),
		 readl(SAR_REG_TSQT),
		 readl(SAR_REG_TSQH));

	stat = le32_to_cpu(tsqe->word_2);

	if (stat & SAR_TSQE_INVALID)
		return;

	do {
		TXPRINTK("tsqe: 0x%p [0x%08x 0x%08x]\n", tsqe,
			 le32_to_cpu(tsqe->word_1),
			 le32_to_cpu(tsqe->word_2));

		switch (stat & SAR_TSQE_TYPE) {
		case SAR_TSQE_TYPE_TIMER:
			TXPRINTK("%s: Timer RollOver detected.\n", card->name);
			break;

		case SAR_TSQE_TYPE_IDLE:

			conn = le32_to_cpu(tsqe->word_1);

			if (SAR_TSQE_TAG(stat) == 0x10) {
#ifdef	NOTDEF
				printk("%s: Connection %d halted.\n",
				       card->name,
				       le32_to_cpu(tsqe->word_1) & 0x1fff);
#endif
				break;
			}

			vc = card->vcs[conn & 0x1fff];
			if (!vc) {
				printk("%s: could not find VC from conn %d\n",
				       card->name, conn & 0x1fff);
				break;
			}

			printk("%s: Connection %d IDLE.\n",
			       card->name, vc->index);

			set_bit(VCF_IDLE, &vc->flags);
			break;

		case SAR_TSQE_TYPE_TSR:

			conn = le32_to_cpu(tsqe->word_1);

			vc = card->vcs[conn & 0x1fff];
			if (!vc) {
				printk("%s: no VC at index %d\n",
				       card->name,
				       le32_to_cpu(tsqe->word_1) & 0x1fff);
				break;
			}

			drain_scq(card, vc);
			break;

		case SAR_TSQE_TYPE_TBD_COMP:

			conn = le32_to_cpu(tsqe->word_1);

			vpi = (conn >> SAR_TBD_VPI_SHIFT) & 0x00ff;
			vci = (conn >> SAR_TBD_VCI_SHIFT) & 0xffff;

			if (vpi >= (1 << card->vpibits) ||
			    vci >= (1 << card->vcibits)) {
				printk("%s: TBD complete: "
				       "out of range VPI.VCI %u.%u\n",
				       card->name, vpi, vci);
				break;
			}

			vc = card->vcs[VPCI2VC(card, vpi, vci)];
			if (!vc) {
				printk("%s: TBD complete: "
				       "no VC at VPI.VCI %u.%u\n",
				       card->name, vpi, vci);
				break;
			}

			drain_scq(card, vc);
			break;
		}

		tsqe->word_2 = cpu_to_le32(SAR_TSQE_INVALID);

		card->tsq.next = tsqe;
		if (card->tsq.next == card->tsq.last)
			tsqe = card->tsq.base;
		else
			tsqe = card->tsq.next + 1;

		TXPRINTK("tsqe: %p: base %p, next %p, last %p\n", tsqe,
			 card->tsq.base, card->tsq.next, card->tsq.last);

		stat = le32_to_cpu(tsqe->word_2);

	} while (!(stat & SAR_TSQE_INVALID));

	writel((unsigned long)card->tsq.next - (unsigned long)card->tsq.base,
	       SAR_REG_TSQH);

	XPRINTK("idt77252_tx-after writel%d: TSQ head = 0x%x, tail = 0x%x, next = 0x%p.\n",
		card->index, readl(SAR_REG_TSQH),
		readl(SAR_REG_TSQT), card->tsq.next);
}


static void
tst_timer(unsigned long data)
{
	struct idt77252_dev *card = (struct idt77252_dev *)data;
	unsigned long base, idle, jump;
	unsigned long flags;
	u32 pc;
	int e;

	spin_lock_irqsave(&card->tst_lock, flags);

	base = card->tst[card->tst_index];
	idle = card->tst[card->tst_index ^ 1];

	if (test_bit(TST_SWITCH_WAIT, &card->tst_state)) {
		jump = base + card->tst_size - 2;

		pc = readl(SAR_REG_NOW) >> 2;
		if ((pc ^ idle) & ~(card->tst_size - 1)) {
			mod_timer(&card->tst_timer, jiffies + 1);
			goto out;
		}

		clear_bit(TST_SWITCH_WAIT, &card->tst_state);

		card->tst_index ^= 1;
		write_sram(card, jump, TSTE_OPC_JMP | (base << 2));

		base = card->tst[card->tst_index];
		idle = card->tst[card->tst_index ^ 1];

		for (e = 0; e < card->tst_size - 2; e++) {
			if (card->soft_tst[e].tste & TSTE_PUSH_IDLE) {
				write_sram(card, idle + e,
					   card->soft_tst[e].tste & TSTE_MASK);
				card->soft_tst[e].tste &= ~(TSTE_PUSH_IDLE);
			}
		}
	}

	if (test_and_clear_bit(TST_SWITCH_PENDING, &card->tst_state)) {

		for (e = 0; e < card->tst_size - 2; e++) {
			if (card->soft_tst[e].tste & TSTE_PUSH_ACTIVE) {
				write_sram(card, idle + e,
					   card->soft_tst[e].tste & TSTE_MASK);
				card->soft_tst[e].tste &= ~(TSTE_PUSH_ACTIVE);
				card->soft_tst[e].tste |= TSTE_PUSH_IDLE;
			}
		}

		jump = base + card->tst_size - 2;

		write_sram(card, jump, TSTE_OPC_NULL);
		set_bit(TST_SWITCH_WAIT, &card->tst_state);

		mod_timer(&card->tst_timer, jiffies + 1);
	}

out:
	spin_unlock_irqrestore(&card->tst_lock, flags);
}

static int
__fill_tst(struct idt77252_dev *card, struct vc_map *vc,
	   int n, unsigned int opc)
{
	unsigned long cl, avail;
	unsigned long idle;
	int e, r;
	u32 data;

	avail = card->tst_size - 2;
	for (e = 0; e < avail; e++) {
		if (card->soft_tst[e].vc == NULL)
			break;
	}
	if (e >= avail) {
		printk("%s: No free TST entries found\n", card->name);
		return -1;
	}

	NPRINTK("%s: conn %d: first TST entry at %d.\n",
		card->name, vc ? vc->index : -1, e);

	r = n;
	cl = avail;
	data = opc & TSTE_OPC_MASK;
	if (vc && (opc != TSTE_OPC_NULL))
		data = opc | vc->index;

	idle = card->tst[card->tst_index ^ 1];

	/*
	 * Fill Soft TST.
	 */
	while (r > 0) {
		if ((cl >= avail) && (card->soft_tst[e].vc == NULL)) {
			if (vc)
				card->soft_tst[e].vc = vc;
			else
				card->soft_tst[e].vc = (void *)-1;

			card->soft_tst[e].tste = data;
			if (timer_pending(&card->tst_timer))
				card->soft_tst[e].tste |= TSTE_PUSH_ACTIVE;
			else {
				write_sram(card, idle + e, data);
				card->soft_tst[e].tste |= TSTE_PUSH_IDLE;
			}

			cl -= card->tst_size;
			r--;
		}

		if (++e == avail)
			e = 0;
		cl += n;
	}

	return 0;
}

static int
fill_tst(struct idt77252_dev *card, struct vc_map *vc, int n, unsigned int opc)
{
	unsigned long flags;
	int res;

	spin_lock_irqsave(&card->tst_lock, flags);

	res = __fill_tst(card, vc, n, opc);

	set_bit(TST_SWITCH_PENDING, &card->tst_state);
	if (!timer_pending(&card->tst_timer))
		mod_timer(&card->tst_timer, jiffies + 1);

	spin_unlock_irqrestore(&card->tst_lock, flags);
	return res;
}

static int
__clear_tst(struct idt77252_dev *card, struct vc_map *vc)
{
	unsigned long idle;
	int e;

	idle = card->tst[card->tst_index ^ 1];

	for (e = 0; e < card->tst_size - 2; e++) {
		if (card->soft_tst[e].vc == vc) {
			card->soft_tst[e].vc = NULL;

			card->soft_tst[e].tste = TSTE_OPC_VAR;
			if (timer_pending(&card->tst_timer))
				card->soft_tst[e].tste |= TSTE_PUSH_ACTIVE;
			else {
				write_sram(card, idle + e, TSTE_OPC_VAR);
				card->soft_tst[e].tste |= TSTE_PUSH_IDLE;
			}
		}
	}

	return 0;
}

static int
clear_tst(struct idt77252_dev *card, struct vc_map *vc)
{
	unsigned long flags;
	int res;

	spin_lock_irqsave(&card->tst_lock, flags);

	res = __clear_tst(card, vc);

	set_bit(TST_SWITCH_PENDING, &card->tst_state);
	if (!timer_pending(&card->tst_timer))
		mod_timer(&card->tst_timer, jiffies + 1);

	spin_unlock_irqrestore(&card->tst_lock, flags);
	return res;
}

static int
change_tst(struct idt77252_dev *card, struct vc_map *vc,
	   int n, unsigned int opc)
{
	unsigned long flags;
	int res;

	spin_lock_irqsave(&card->tst_lock, flags);

	__clear_tst(card, vc);
	res = __fill_tst(card, vc, n, opc);

	set_bit(TST_SWITCH_PENDING, &card->tst_state);
	if (!timer_pending(&card->tst_timer))
		mod_timer(&card->tst_timer, jiffies + 1);

	spin_unlock_irqrestore(&card->tst_lock, flags);
	return res;
}


static int
set_tct(struct idt77252_dev *card, struct vc_map *vc)
{
	unsigned long tct;

	tct = (unsigned long) (card->tct_base + vc->index * SAR_SRAM_TCT_SIZE);

	switch (vc->class) {
	case SCHED_CBR:
		OPRINTK("%s: writing TCT at 0x%lx, SCD 0x%lx.\n",
		        card->name, tct, vc->scq->scd);

		write_sram(card, tct + 0, TCT_CBR | vc->scq->scd);
		write_sram(card, tct + 1, 0);
		write_sram(card, tct + 2, 0);
		write_sram(card, tct + 3, 0);
		write_sram(card, tct + 4, 0);
		write_sram(card, tct + 5, 0);
		write_sram(card, tct + 6, 0);
		write_sram(card, tct + 7, 0);
		break;

	case SCHED_UBR:
		OPRINTK("%s: writing TCT at 0x%lx, SCD 0x%lx.\n",
		        card->name, tct, vc->scq->scd);

		write_sram(card, tct + 0, TCT_UBR | vc->scq->scd);
		write_sram(card, tct + 1, 0);
		write_sram(card, tct + 2, TCT_TSIF);
		write_sram(card, tct + 3, TCT_HALT | TCT_IDLE);
		write_sram(card, tct + 4, 0);
		write_sram(card, tct + 5, vc->init_er);
		write_sram(card, tct + 6, 0);
		write_sram(card, tct + 7, TCT_FLAG_UBR);
		break;

	case SCHED_VBR:
	case SCHED_ABR:
	default:
		return -ENOSYS;
	}

	return 0;
}

/*****************************************************************************/
/*                                                                           */
/* FBQ Handling                                                              */
/*                                                                           */
/*****************************************************************************/

static __inline__ int
idt77252_fbq_level(struct idt77252_dev *card, int queue)
{
	return (readl(SAR_REG_STAT) >> (16 + (queue << 2))) & 0x0f;
}

static __inline__ int
idt77252_fbq_full(struct idt77252_dev *card, int queue)
{
	return (readl(SAR_REG_STAT) >> (16 + (queue << 2))) == 0x0f;
}

static int
push_rx_skb(struct idt77252_dev *card, struct sk_buff *skb, int queue)
{
	unsigned long flags;
	u32 handle;
	u32 addr;

	skb->data = skb->head;
	skb_reset_tail_pointer(skb);
	skb->len = 0;

	skb_reserve(skb, 16);

	switch (queue) {
	case 0:
		skb_put(skb, SAR_FB_SIZE_0);
		break;
	case 1:
		skb_put(skb, SAR_FB_SIZE_1);
		break;
	case 2:
		skb_put(skb, SAR_FB_SIZE_2);
		break;
	case 3:
		skb_put(skb, SAR_FB_SIZE_3);
		break;
	default:
		return -1;
	}

	if (idt77252_fbq_full(card, queue))
		return -1;

	memset(&skb->data[(skb->len & ~(0x3f)) - 64], 0, 2 * sizeof(u32));

	handle = IDT77252_PRV_POOL(skb);
	addr = IDT77252_PRV_PADDR(skb);

	spin_lock_irqsave(&card->cmd_lock, flags);
	writel(handle, card->fbq[queue]);
	writel(addr, card->fbq[queue]);
	spin_unlock_irqrestore(&card->cmd_lock, flags);

	return 0;
}

static void
add_rx_skb(struct idt77252_dev *card, int queue,
	   unsigned int size, unsigned int count)
{
	struct sk_buff *skb;
	dma_addr_t paddr;
	u32 handle;

	while (count--) {
		skb = dev_alloc_skb(size);
		if (!skb)
			return;

		if (sb_pool_add(card, skb, queue)) {
			printk("%s: SB POOL full\n", __func__);
			goto outfree;
		}

		paddr = pci_map_single(card->pcidev, skb->data,
				       skb_end_pointer(skb) - skb->data,
				       PCI_DMA_FROMDEVICE);
		IDT77252_PRV_PADDR(skb) = paddr;

		if (push_rx_skb(card, skb, queue)) {
			printk("%s: FB QUEUE full\n", __func__);
			goto outunmap;
		}
	}

	return;

outunmap:
	pci_unmap_single(card->pcidev, IDT77252_PRV_PADDR(skb),
			 skb_end_pointer(skb) - skb->data, PCI_DMA_FROMDEVICE);

	handle = IDT77252_PRV_POOL(skb);
	card->sbpool[POOL_QUEUE(handle)].skb[POOL_INDEX(handle)] = NULL;

outfree:
	dev_kfree_skb(skb);
}


static void
recycle_rx_skb(struct idt77252_dev *card, struct sk_buff *skb)
{
	u32 handle = IDT77252_PRV_POOL(skb);
	int err;

	pci_dma_sync_single_for_device(card->pcidev, IDT77252_PRV_PADDR(skb),
				       skb_end_pointer(skb) - skb->data,
				       PCI_DMA_FROMDEVICE);

	err = push_rx_skb(card, skb, POOL_QUEUE(handle));
	if (err) {
		pci_unmap_single(card->pcidev, IDT77252_PRV_PADDR(skb),
				 skb_end_pointer(skb) - skb->data,
				 PCI_DMA_FROMDEVICE);
		sb_pool_remove(card, skb);
		dev_kfree_skb(skb);
	}
}

static void
flush_rx_pool(struct idt77252_dev *card, struct rx_pool *rpp)
{
	skb_queue_head_init(&rpp->queue);
	rpp->len = 0;
}

static void
recycle_rx_pool_skb(struct idt77252_dev *card, struct rx_pool *rpp)
{
	struct sk_buff *skb, *tmp;

	skb_queue_walk_safe(&rpp->queue, skb, tmp)
		recycle_rx_skb(card, skb);

	flush_rx_pool(card, rpp);
}

/*****************************************************************************/
/*                                                                           */
/* ATM Interface                                                             */
/*                                                                           */
/*****************************************************************************/

static void
idt77252_phy_put(struct atm_dev *dev, unsigned char value, unsigned long addr)
{
	write_utility(dev->dev_data, 0x100 + (addr & 0x1ff), value);
}

static unsigned char
idt77252_phy_get(struct atm_dev *dev, unsigned long addr)
{
	return read_utility(dev->dev_data, 0x100 + (addr & 0x1ff));
}

static inline int
idt77252_send_skb(struct atm_vcc *vcc, struct sk_buff *skb, int oam)
{
	struct atm_dev *dev = vcc->dev;
	struct idt77252_dev *card = dev->dev_data;
	struct vc_map *vc = vcc->dev_data;
	int err;

	if (vc == NULL) {
		printk("%s: NULL connection in send().\n", card->name);
		atomic_inc(&vcc->stats->tx_err);
		dev_kfree_skb(skb);
		return -EINVAL;
	}
	if (!test_bit(VCF_TX, &vc->flags)) {
		printk("%s: Trying to transmit on a non-tx VC.\n", card->name);
		atomic_inc(&vcc->stats->tx_err);
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	switch (vcc->qos.aal) {
	case ATM_AAL0:
	case ATM_AAL1:
	case ATM_AAL5:
		break;
	default:
		printk("%s: Unsupported AAL: %d\n", card->name, vcc->qos.aal);
		atomic_inc(&vcc->stats->tx_err);
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	if (skb_shinfo(skb)->nr_frags != 0) {
		printk("%s: No scatter-gather yet.\n", card->name);
		atomic_inc(&vcc->stats->tx_err);
		dev_kfree_skb(skb);
		return -EINVAL;
	}
	ATM_SKB(skb)->vcc = vcc;

	err = queue_skb(card, vc, skb, oam);
	if (err) {
		atomic_inc(&vcc->stats->tx_err);
		dev_kfree_skb(skb);
		return err;
	}

	return 0;
}

static int idt77252_send(struct atm_vcc *vcc, struct sk_buff *skb)
{
	return idt77252_send_skb(vcc, skb, 0);
}

static int
idt77252_send_oam(struct atm_vcc *vcc, void *cell, int flags)
{
	struct atm_dev *dev = vcc->dev;
	struct idt77252_dev *card = dev->dev_data;
	struct sk_buff *skb;

	skb = dev_alloc_skb(64);
	if (!skb) {
		printk("%s: Out of memory in send_oam().\n", card->name);
		atomic_inc(&vcc->stats->tx_err);
		return -ENOMEM;
	}
	atomic_add(skb->truesize, &sk_atm(vcc)->sk_wmem_alloc);

	memcpy(skb_put(skb, 52), cell, 52);

	return idt77252_send_skb(vcc, skb, 1);
}

static __inline__ unsigned int
idt77252_fls(unsigned int x)
{
	int r = 1;

	if (x == 0)
		return 0;
	if (x & 0xffff0000) {
		x >>= 16;
		r += 16;
	}
	if (x & 0xff00) {
		x >>= 8;
		r += 8;
	}
	if (x & 0xf0) {
		x >>= 4;
		r += 4;
	}
	if (x & 0xc) {
		x >>= 2;
		r += 2;
	}
	if (x & 0x2)
		r += 1;
	return r;
}

static u16
idt77252_int_to_atmfp(unsigned int rate)
{
	u16 m, e;

	if (rate == 0)
		return 0;
	e = idt77252_fls(rate) - 1;
	if (e < 9)
		m = (rate - (1 << e)) << (9 - e);
	else if (e == 9)
		m = (rate - (1 << e));
	else /* e > 9 */
		m = (rate - (1 << e)) >> (e - 9);
	return 0x4000 | (e << 9) | m;
}

static u8
idt77252_rate_logindex(struct idt77252_dev *card, int pcr)
{
	u16 afp;

	afp = idt77252_int_to_atmfp(pcr < 0 ? -pcr : pcr);
	if (pcr < 0)
		return rate_to_log[(afp >> 5) & 0x1ff];
	return rate_to_log[((afp >> 5) + 1) & 0x1ff];
}

static void
idt77252_est_timer(unsigned long data)
{
	struct vc_map *vc = (struct vc_map *)data;
	struct idt77252_dev *card = vc->card;
	struct rate_estimator *est;
	unsigned long flags;
	u32 rate, cps;
	u64 ncells;
	u8 lacr;

	spin_lock_irqsave(&vc->lock, flags);
	est = vc->estimator;
	if (!est)
		goto out;

	ncells = est->cells;

	rate = ((u32)(ncells - est->last_cells)) << (7 - est->interval);
	est->last_cells = ncells;
	est->avcps += ((long)rate - (long)est->avcps) >> est->ewma_log;
	est->cps = (est->avcps + 0x1f) >> 5;

	cps = est->cps;
	if (cps < (est->maxcps >> 4))
		cps = est->maxcps >> 4;

	lacr = idt77252_rate_logindex(card, cps);
	if (lacr > vc->max_er)
		lacr = vc->max_er;

	if (lacr != vc->lacr) {
		vc->lacr = lacr;
		writel(TCMDQ_LACR|(vc->lacr << 16)|vc->index, SAR_REG_TCMDQ);
	}

	est->timer.expires = jiffies + ((HZ / 4) << est->interval);
	add_timer(&est->timer);

out:
	spin_unlock_irqrestore(&vc->lock, flags);
}

static struct rate_estimator *
idt77252_init_est(struct vc_map *vc, int pcr)
{
	struct rate_estimator *est;

	est = kzalloc(sizeof(struct rate_estimator), GFP_KERNEL);
	if (!est)
		return NULL;
	est->maxcps = pcr < 0 ? -pcr : pcr;
	est->cps = est->maxcps;
	est->avcps = est->cps << 5;

	est->interval = 2;		/* XXX: make this configurable */
	est->ewma_log = 2;		/* XXX: make this configurable */
	init_timer(&est->timer);
	est->timer.data = (unsigned long)vc;
	est->timer.function = idt77252_est_timer;

	est->timer.expires = jiffies + ((HZ / 4) << est->interval);
	add_timer(&est->timer);

	return est;
}

static int
idt77252_init_cbr(struct idt77252_dev *card, struct vc_map *vc,
		  struct atm_vcc *vcc, struct atm_qos *qos)
{
	int tst_free, tst_used, tst_entries;
	unsigned long tmpl, modl;
	int tcr, tcra;

	if ((qos->txtp.max_pcr == 0) &&
	    (qos->txtp.pcr == 0) && (qos->txtp.min_pcr == 0)) {
		printk("%s: trying to open a CBR VC with cell rate = 0\n",
		       card->name);
		return -EINVAL;
	}

	tst_used = 0;
	tst_free = card->tst_free;
	if (test_bit(VCF_TX, &vc->flags))
		tst_used = vc->ntste;
	tst_free += tst_used;

	tcr = atm_pcr_goal(&qos->txtp);
	tcra = tcr >= 0 ? tcr : -tcr;

	TXPRINTK("%s: CBR target cell rate = %d\n", card->name, tcra);

	tmpl = (unsigned long) tcra * ((unsigned long) card->tst_size - 2);
	modl = tmpl % (unsigned long)card->utopia_pcr;

	tst_entries = (int) (tmpl / card->utopia_pcr);
	if (tcr > 0) {
		if (modl > 0)
			tst_entries++;
	} else if (tcr == 0) {
		tst_entries = tst_free - SAR_TST_RESERVED;
		if (tst_entries <= 0) {
			printk("%s: no CBR bandwidth free.\n", card->name);
			return -ENOSR;
		}
	}

	if (tst_entries == 0) {
		printk("%s: selected CBR bandwidth < granularity.\n",
		       card->name);
		return -EINVAL;
	}

	if (tst_entries > (tst_free - SAR_TST_RESERVED)) {
		printk("%s: not enough CBR bandwidth free.\n", card->name);
		return -ENOSR;
	}

	vc->ntste = tst_entries;

	card->tst_free = tst_free - tst_entries;
	if (test_bit(VCF_TX, &vc->flags)) {
		if (tst_used == tst_entries)
			return 0;

		OPRINTK("%s: modify %d -> %d entries in TST.\n",
			card->name, tst_used, tst_entries);
		change_tst(card, vc, tst_entries, TSTE_OPC_CBR);
		return 0;
	}

	OPRINTK("%s: setting %d entries in TST.\n", card->name, tst_entries);
	fill_tst(card, vc, tst_entries, TSTE_OPC_CBR);
	return 0;
}

static int
idt77252_init_ubr(struct idt77252_dev *card, struct vc_map *vc,
		  struct atm_vcc *vcc, struct atm_qos *qos)
{
	unsigned long flags;
	int tcr;

	spin_lock_irqsave(&vc->lock, flags);
	if (vc->estimator) {
		del_timer(&vc->estimator->timer);
		kfree(vc->estimator);
		vc->estimator = NULL;
	}
	spin_unlock_irqrestore(&vc->lock, flags);

	tcr = atm_pcr_goal(&qos->txtp);
	if (tcr == 0)
		tcr = card->link_pcr;

	vc->estimator = idt77252_init_est(vc, tcr);

	vc->class = SCHED_UBR;
	vc->init_er = idt77252_rate_logindex(card, tcr);
	vc->lacr = vc->init_er;
	if (tcr < 0)
		vc->max_er = vc->init_er;
	else
		vc->max_er = 0xff;

	return 0;
}

static int
idt77252_init_tx(struct idt77252_dev *card, struct vc_map *vc,
		 struct atm_vcc *vcc, struct atm_qos *qos)
{
	int error;

	if (test_bit(VCF_TX, &vc->flags))
		return -EBUSY;

	switch (qos->txtp.traffic_class) {
		case ATM_CBR:
			vc->class = SCHED_CBR;
			break;

		case ATM_UBR:
			vc->class = SCHED_UBR;
			break;

		case ATM_VBR:
		case ATM_ABR:
		default:
			return -EPROTONOSUPPORT;
	}

	vc->scq = alloc_scq(card, vc->class);
	if (!vc->scq) {
		printk("%s: can't get SCQ.\n", card->name);
		return -ENOMEM;
	}

	vc->scq->scd = get_free_scd(card, vc);
	if (vc->scq->scd == 0) {
		printk("%s: no SCD available.\n", card->name);
		free_scq(card, vc->scq);
		return -ENOMEM;
	}

	fill_scd(card, vc->scq, vc->class);

	if (set_tct(card, vc)) {
		printk("%s: class %d not supported.\n",
		       card->name, qos->txtp.traffic_class);

		card->scd2vc[vc->scd_index] = NULL;
		free_scq(card, vc->scq);
		return -EPROTONOSUPPORT;
	}

	switch (vc->class) {
		case SCHED_CBR:
			error = idt77252_init_cbr(card, vc, vcc, qos);
			if (error) {
				card->scd2vc[vc->scd_index] = NULL;
				free_scq(card, vc->scq);
				return error;
			}

			clear_bit(VCF_IDLE, &vc->flags);
			writel(TCMDQ_START | vc->index, SAR_REG_TCMDQ);
			break;

		case SCHED_UBR:
			error = idt77252_init_ubr(card, vc, vcc, qos);
			if (error) {
				card->scd2vc[vc->scd_index] = NULL;
				free_scq(card, vc->scq);
				return error;
			}

			set_bit(VCF_IDLE, &vc->flags);
			break;
	}

	vc->tx_vcc = vcc;
	set_bit(VCF_TX, &vc->flags);
	return 0;
}

static int
idt77252_init_rx(struct idt77252_dev *card, struct vc_map *vc,
		 struct atm_vcc *vcc, struct atm_qos *qos)
{
	unsigned long flags;
	unsigned long addr;
	u32 rcte = 0;

	if (test_bit(VCF_RX, &vc->flags))
		return -EBUSY;

	vc->rx_vcc = vcc;
	set_bit(VCF_RX, &vc->flags);

	if ((vcc->vci == 3) || (vcc->vci == 4))
		return 0;

	flush_rx_pool(card, &vc->rcv.rx_pool);

	rcte |= SAR_RCTE_CONNECTOPEN;
	rcte |= SAR_RCTE_RAWCELLINTEN;

	switch (qos->aal) {
		case ATM_AAL0:
			rcte |= SAR_RCTE_RCQ;
			break;
		case ATM_AAL1:
			rcte |= SAR_RCTE_OAM; /* Let SAR drop Video */
			break;
		case ATM_AAL34:
			rcte |= SAR_RCTE_AAL34;
			break;
		case ATM_AAL5:
			rcte |= SAR_RCTE_AAL5;
			break;
		default:
			rcte |= SAR_RCTE_RCQ;
			break;
	}

	if (qos->aal != ATM_AAL5)
		rcte |= SAR_RCTE_FBP_1;
	else if (qos->rxtp.max_sdu > SAR_FB_SIZE_2)
		rcte |= SAR_RCTE_FBP_3;
	else if (qos->rxtp.max_sdu > SAR_FB_SIZE_1)
		rcte |= SAR_RCTE_FBP_2;
	else if (qos->rxtp.max_sdu > SAR_FB_SIZE_0)
		rcte |= SAR_RCTE_FBP_1;
	else
		rcte |= SAR_RCTE_FBP_01;

	addr = card->rct_base + (vc->index << 2);

	OPRINTK("%s: writing RCT at 0x%lx\n", card->name, addr);
	write_sram(card, addr, rcte);

	spin_lock_irqsave(&card->cmd_lock, flags);
	writel(SAR_CMD_OPEN_CONNECTION | (addr << 2), SAR_REG_CMD);
	waitfor_idle(card);
	spin_unlock_irqrestore(&card->cmd_lock, flags);

	return 0;
}

static int
idt77252_open(struct atm_vcc *vcc)
{
	struct atm_dev *dev = vcc->dev;
	struct idt77252_dev *card = dev->dev_data;
	struct vc_map *vc;
	unsigned int index;
	unsigned int inuse;
	int error;
	int vci = vcc->vci;
	short vpi = vcc->vpi;

	if (vpi == ATM_VPI_UNSPEC || vci == ATM_VCI_UNSPEC)
		return 0;

	if (vpi >= (1 << card->vpibits)) {
		printk("%s: unsupported VPI: %d\n", card->name, vpi);
		return -EINVAL;
	}

	if (vci >= (1 << card->vcibits)) {
		printk("%s: unsupported VCI: %d\n", card->name, vci);
		return -EINVAL;
	}

	set_bit(ATM_VF_ADDR, &vcc->flags);

	mutex_lock(&card->mutex);

	OPRINTK("%s: opening vpi.vci: %d.%d\n", card->name, vpi, vci);

	switch (vcc->qos.aal) {
	case ATM_AAL0:
	case ATM_AAL1:
	case ATM_AAL5:
		break;
	default:
		printk("%s: Unsupported AAL: %d\n", card->name, vcc->qos.aal);
		mutex_unlock(&card->mutex);
		return -EPROTONOSUPPORT;
	}

	index = VPCI2VC(card, vpi, vci);
	if (!card->vcs[index]) {
		card->vcs[index] = kzalloc(sizeof(struct vc_map), GFP_KERNEL);
		if (!card->vcs[index]) {
			printk("%s: can't alloc vc in open()\n", card->name);
			mutex_unlock(&card->mutex);
			return -ENOMEM;
		}
		card->vcs[index]->card = card;
		card->vcs[index]->index = index;

		spin_lock_init(&card->vcs[index]->lock);
	}
	vc = card->vcs[index];

	vcc->dev_data = vc;

	IPRINTK("%s: idt77252_open: vc = %d (%d.%d) %s/%s (max RX SDU: %u)\n",
	        card->name, vc->index, vcc->vpi, vcc->vci,
	        vcc->qos.rxtp.traffic_class != ATM_NONE ? "rx" : "--",
	        vcc->qos.txtp.traffic_class != ATM_NONE ? "tx" : "--",
	        vcc->qos.rxtp.max_sdu);

	inuse = 0;
	if (vcc->qos.txtp.traffic_class != ATM_NONE &&
	    test_bit(VCF_TX, &vc->flags))
		inuse = 1;
	if (vcc->qos.rxtp.traffic_class != ATM_NONE &&
	    test_bit(VCF_RX, &vc->flags))
		inuse += 2;

	if (inuse) {
		printk("%s: %s vci already in use.\n", card->name,
		       inuse == 1 ? "tx" : inuse == 2 ? "rx" : "tx and rx");
		mutex_unlock(&card->mutex);
		return -EADDRINUSE;
	}

	if (vcc->qos.txtp.traffic_class != ATM_NONE) {
		error = idt77252_init_tx(card, vc, vcc, &vcc->qos);
		if (error) {
			mutex_unlock(&card->mutex);
			return error;
		}
	}

	if (vcc->qos.rxtp.traffic_class != ATM_NONE) {
		error = idt77252_init_rx(card, vc, vcc, &vcc->qos);
		if (error) {
			mutex_unlock(&card->mutex);
			return error;
		}
	}

	set_bit(ATM_VF_READY, &vcc->flags);

	mutex_unlock(&card->mutex);
	return 0;
}

static void
idt77252_close(struct atm_vcc *vcc)
{
	struct atm_dev *dev = vcc->dev;
	struct idt77252_dev *card = dev->dev_data;
	struct vc_map *vc = vcc->dev_data;
	unsigned long flags;
	unsigned long addr;
	unsigned long timeout;

	mutex_lock(&card->mutex);

	IPRINTK("%s: idt77252_close: vc = %d (%d.%d)\n",
		card->name, vc->index, vcc->vpi, vcc->vci);

	clear_bit(ATM_VF_READY, &vcc->flags);

	if (vcc->qos.rxtp.traffic_class != ATM_NONE) {

		spin_lock_irqsave(&vc->lock, flags);
		clear_bit(VCF_RX, &vc->flags);
		vc->rx_vcc = NULL;
		spin_unlock_irqrestore(&vc->lock, flags);

		if ((vcc->vci == 3) || (vcc->vci == 4))
			goto done;

		addr = card->rct_base + vc->index * SAR_SRAM_RCT_SIZE;

		spin_lock_irqsave(&card->cmd_lock, flags);
		writel(SAR_CMD_CLOSE_CONNECTION | (addr << 2), SAR_REG_CMD);
		waitfor_idle(card);
		spin_unlock_irqrestore(&card->cmd_lock, flags);

		if (skb_queue_len(&vc->rcv.rx_pool.queue) != 0) {
			DPRINTK("%s: closing a VC with pending rx buffers.\n",
				card->name);

			recycle_rx_pool_skb(card, &vc->rcv.rx_pool);
		}
	}

done:
	if (vcc->qos.txtp.traffic_class != ATM_NONE) {

		spin_lock_irqsave(&vc->lock, flags);
		clear_bit(VCF_TX, &vc->flags);
		clear_bit(VCF_IDLE, &vc->flags);
		clear_bit(VCF_RSV, &vc->flags);
		vc->tx_vcc = NULL;

		if (vc->estimator) {
			del_timer(&vc->estimator->timer);
			kfree(vc->estimator);
			vc->estimator = NULL;
		}
		spin_unlock_irqrestore(&vc->lock, flags);

		timeout = 5 * 1000;
		while (atomic_read(&vc->scq->used) > 0) {
			timeout = msleep_interruptible(timeout);
			if (!timeout)
				break;
		}
		if (!timeout)
			printk("%s: SCQ drain timeout: %u used\n",
			       card->name, atomic_read(&vc->scq->used));

		writel(TCMDQ_HALT | vc->index, SAR_REG_TCMDQ);
		clear_scd(card, vc->scq, vc->class);

		if (vc->class == SCHED_CBR) {
			clear_tst(card, vc);
			card->tst_free += vc->ntste;
			vc->ntste = 0;
		}

		card->scd2vc[vc->scd_index] = NULL;
		free_scq(card, vc->scq);
	}

	mutex_unlock(&card->mutex);
}

static int
idt77252_change_qos(struct atm_vcc *vcc, struct atm_qos *qos, int flags)
{
	struct atm_dev *dev = vcc->dev;
	struct idt77252_dev *card = dev->dev_data;
	struct vc_map *vc = vcc->dev_data;
	int error = 0;

	mutex_lock(&card->mutex);

	if (qos->txtp.traffic_class != ATM_NONE) {
	    	if (!test_bit(VCF_TX, &vc->flags)) {
			error = idt77252_init_tx(card, vc, vcc, qos);
			if (error)
				goto out;
		} else {
			switch (qos->txtp.traffic_class) {
			case ATM_CBR:
				error = idt77252_init_cbr(card, vc, vcc, qos);
				if (error)
					goto out;
				break;

			case ATM_UBR:
				error = idt77252_init_ubr(card, vc, vcc, qos);
				if (error)
					goto out;

				if (!test_bit(VCF_IDLE, &vc->flags)) {
					writel(TCMDQ_LACR | (vc->lacr << 16) |
					       vc->index, SAR_REG_TCMDQ);
				}
				break;

			case ATM_VBR:
			case ATM_ABR:
				error = -EOPNOTSUPP;
				goto out;
			}
		}
	}

	if ((qos->rxtp.traffic_class != ATM_NONE) &&
	    !test_bit(VCF_RX, &vc->flags)) {
		error = idt77252_init_rx(card, vc, vcc, qos);
		if (error)
			goto out;
	}

	memcpy(&vcc->qos, qos, sizeof(struct atm_qos));

	set_bit(ATM_VF_HASQOS, &vcc->flags);

out:
	mutex_unlock(&card->mutex);
	return error;
}

static int
idt77252_proc_read(struct atm_dev *dev, loff_t * pos, char *page)
{
	struct idt77252_dev *card = dev->dev_data;
	int i, left;

	left = (int) *pos;
	if (!left--)
		return sprintf(page, "IDT77252 Interrupts:\n");
	if (!left--)
		return sprintf(page, "TSIF:  %lu\n", card->irqstat[15]);
	if (!left--)
		return sprintf(page, "TXICP: %lu\n", card->irqstat[14]);
	if (!left--)
		return sprintf(page, "TSQF:  %lu\n", card->irqstat[12]);
	if (!left--)
		return sprintf(page, "TMROF: %lu\n", card->irqstat[11]);
	if (!left--)
		return sprintf(page, "PHYI:  %lu\n", card->irqstat[10]);
	if (!left--)
		return sprintf(page, "FBQ3A: %lu\n", card->irqstat[8]);
	if (!left--)
		return sprintf(page, "FBQ2A: %lu\n", card->irqstat[7]);
	if (!left--)
		return sprintf(page, "RSQF:  %lu\n", card->irqstat[6]);
	if (!left--)
		return sprintf(page, "EPDU:  %lu\n", card->irqstat[5]);
	if (!left--)
		return sprintf(page, "RAWCF: %lu\n", card->irqstat[4]);
	if (!left--)
		return sprintf(page, "FBQ1A: %lu\n", card->irqstat[3]);
	if (!left--)
		return sprintf(page, "FBQ0A: %lu\n", card->irqstat[2]);
	if (!left--)
		return sprintf(page, "RSQAF: %lu\n", card->irqstat[1]);
	if (!left--)
		return sprintf(page, "IDT77252 Transmit Connection Table:\n");

	for (i = 0; i < card->tct_size; i++) {
		unsigned long tct;
		struct atm_vcc *vcc;
		struct vc_map *vc;
		char *p;

		vc = card->vcs[i];
		if (!vc)
			continue;

		vcc = NULL;
		if (vc->tx_vcc)
			vcc = vc->tx_vcc;
		if (!vcc)
			continue;
		if (left--)
			continue;

		p = page;
		p += sprintf(p, "  %4u: %u.%u: ", i, vcc->vpi, vcc->vci);
		tct = (unsigned long) (card->tct_base + i * SAR_SRAM_TCT_SIZE);

		for (i = 0; i < 8; i++)
			p += sprintf(p, " %08x", read_sram(card, tct + i));
		p += sprintf(p, "\n");
		return p - page;
	}
	return 0;
}

/*****************************************************************************/
/*                                                                           */
/* Interrupt handler                                                         */
/*                                                                           */
/*****************************************************************************/

static void
idt77252_collect_stat(struct idt77252_dev *card)
{
	u32 cdc, vpec, icc;

	cdc = readl(SAR_REG_CDC);
	vpec = readl(SAR_REG_VPEC);
	icc = readl(SAR_REG_ICC);

#ifdef	NOTDEF
	printk("%s:", card->name);

	if (cdc & 0x7f0000) {
		char *s = "";

		printk(" [");
		if (cdc & (1 << 22)) {
			printk("%sRM ID", s);
			s = " | ";
		}
		if (cdc & (1 << 21)) {
			printk("%sCON TAB", s);
			s = " | ";
		}
		if (cdc & (1 << 20)) {
			printk("%sNO FB", s);
			s = " | ";
		}
		if (cdc & (1 << 19)) {
			printk("%sOAM CRC", s);
			s = " | ";
		}
		if (cdc & (1 << 18)) {
			printk("%sRM CRC", s);
			s = " | ";
		}
		if (cdc & (1 << 17)) {
			printk("%sRM FIFO", s);
			s = " | ";
		}
		if (cdc & (1 << 16)) {
			printk("%sRX FIFO", s);
			s = " | ";
		}
		printk("]");
	}

	printk(" CDC %04x, VPEC %04x, ICC: %04x\n",
	       cdc & 0xffff, vpec & 0xffff, icc & 0xffff);
#endif
}

static irqreturn_t
idt77252_interrupt(int irq, void *dev_id)
{
	struct idt77252_dev *card = dev_id;
	u32 stat;

	stat = readl(SAR_REG_STAT) & 0xffff;
	if (!stat)	/* no interrupt for us */
		return IRQ_NONE;

	if (test_and_set_bit(IDT77252_BIT_INTERRUPT, &card->flags)) {
		printk("%s: Re-entering irq_handler()\n", card->name);
		goto out;
	}

	writel(stat, SAR_REG_STAT);	/* reset interrupt */

	if (stat & SAR_STAT_TSIF) {	/* entry written to TSQ  */
		INTPRINTK("%s: TSIF\n", card->name);
		card->irqstat[15]++;
		idt77252_tx(card);
	}
	if (stat & SAR_STAT_TXICP) {	/* Incomplete CS-PDU has  */
		INTPRINTK("%s: TXICP\n", card->name);
		card->irqstat[14]++;
#ifdef CONFIG_ATM_IDT77252_DEBUG
		idt77252_tx_dump(card);
#endif
	}
	if (stat & SAR_STAT_TSQF) {	/* TSQ 7/8 full           */
		INTPRINTK("%s: TSQF\n", card->name);
		card->irqstat[12]++;
		idt77252_tx(card);
	}
	if (stat & SAR_STAT_TMROF) {	/* Timer overflow         */
		INTPRINTK("%s: TMROF\n", card->name);
		card->irqstat[11]++;
		idt77252_collect_stat(card);
	}

	if (stat & SAR_STAT_EPDU) {	/* Got complete CS-PDU    */
		INTPRINTK("%s: EPDU\n", card->name);
		card->irqstat[5]++;
		idt77252_rx(card);
	}
	if (stat & SAR_STAT_RSQAF) {	/* RSQ is 7/8 full        */
		INTPRINTK("%s: RSQAF\n", card->name);
		card->irqstat[1]++;
		idt77252_rx(card);
	}
	if (stat & SAR_STAT_RSQF) {	/* RSQ is full            */
		INTPRINTK("%s: RSQF\n", card->name);
		card->irqstat[6]++;
		idt77252_rx(card);
	}
	if (stat & SAR_STAT_RAWCF) {	/* Raw cell received      */
		INTPRINTK("%s: RAWCF\n", card->name);
		card->irqstat[4]++;
		idt77252_rx_raw(card);
	}

	if (stat & SAR_STAT_PHYI) {	/* PHY device interrupt   */
		INTPRINTK("%s: PHYI", card->name);
		card->irqstat[10]++;
		if (card->atmdev->phy && card->atmdev->phy->interrupt)
			card->atmdev->phy->interrupt(card->atmdev);
	}

	if (stat & (SAR_STAT_FBQ0A | SAR_STAT_FBQ1A |
		    SAR_STAT_FBQ2A | SAR_STAT_FBQ3A)) {

		writel(readl(SAR_REG_CFG) & ~(SAR_CFG_FBIE), SAR_REG_CFG);

		INTPRINTK("%s: FBQA: %04x\n", card->name, stat);

		if (stat & SAR_STAT_FBQ0A)
			card->irqstat[2]++;
		if (stat & SAR_STAT_FBQ1A)
			card->irqstat[3]++;
		if (stat & SAR_STAT_FBQ2A)
			card->irqstat[7]++;
		if (stat & SAR_STAT_FBQ3A)
			card->irqstat[8]++;

		schedule_work(&card->tqueue);
	}

out:
	clear_bit(IDT77252_BIT_INTERRUPT, &card->flags);
	return IRQ_HANDLED;
}

static void
idt77252_softint(struct work_struct *work)
{
	struct idt77252_dev *card =
		container_of(work, struct idt77252_dev, tqueue);
	u32 stat;
	int done;

	for (done = 1; ; done = 1) {
		stat = readl(SAR_REG_STAT) >> 16;

		if ((stat & 0x0f) < SAR_FBQ0_HIGH) {
			add_rx_skb(card, 0, SAR_FB_SIZE_0, 32);
			done = 0;
		}

		stat >>= 4;
		if ((stat & 0x0f) < SAR_FBQ1_HIGH) {
			add_rx_skb(card, 1, SAR_FB_SIZE_1, 32);
			done = 0;
		}

		stat >>= 4;
		if ((stat & 0x0f) < SAR_FBQ2_HIGH) {
			add_rx_skb(card, 2, SAR_FB_SIZE_2, 32);
			done = 0;
		}

		stat >>= 4;
		if ((stat & 0x0f) < SAR_FBQ3_HIGH) {
			add_rx_skb(card, 3, SAR_FB_SIZE_3, 32);
			done = 0;
		}

		if (done)
			break;
	}

	writel(readl(SAR_REG_CFG) | SAR_CFG_FBIE, SAR_REG_CFG);
}


static int
open_card_oam(struct idt77252_dev *card)
{
	unsigned long flags;
	unsigned long addr;
	struct vc_map *vc;
	int vpi, vci;
	int index;
	u32 rcte;

	for (vpi = 0; vpi < (1 << card->vpibits); vpi++) {
		for (vci = 3; vci < 5; vci++) {
			index = VPCI2VC(card, vpi, vci);

			vc = kzalloc(sizeof(struct vc_map), GFP_KERNEL);
			if (!vc) {
				printk("%s: can't alloc vc\n", card->name);
				return -ENOMEM;
			}
			vc->index = index;
			card->vcs[index] = vc;

			flush_rx_pool(card, &vc->rcv.rx_pool);

			rcte = SAR_RCTE_CONNECTOPEN |
			       SAR_RCTE_RAWCELLINTEN |
			       SAR_RCTE_RCQ |
			       SAR_RCTE_FBP_1;

			addr = card->rct_base + (vc->index << 2);
			write_sram(card, addr, rcte);

			spin_lock_irqsave(&card->cmd_lock, flags);
			writel(SAR_CMD_OPEN_CONNECTION | (addr << 2),
			       SAR_REG_CMD);
			waitfor_idle(card);
			spin_unlock_irqrestore(&card->cmd_lock, flags);
		}
	}

	return 0;
}

static void
close_card_oam(struct idt77252_dev *card)
{
	unsigned long flags;
	unsigned long addr;
	struct vc_map *vc;
	int vpi, vci;
	int index;

	for (vpi = 0; vpi < (1 << card->vpibits); vpi++) {
		for (vci = 3; vci < 5; vci++) {
			index = VPCI2VC(card, vpi, vci);
			vc = card->vcs[index];

			addr = card->rct_base + vc->index * SAR_SRAM_RCT_SIZE;

			spin_lock_irqsave(&card->cmd_lock, flags);
			writel(SAR_CMD_CLOSE_CONNECTION | (addr << 2),
			       SAR_REG_CMD);
			waitfor_idle(card);
			spin_unlock_irqrestore(&card->cmd_lock, flags);

			if (skb_queue_len(&vc->rcv.rx_pool.queue) != 0) {
				DPRINTK("%s: closing a VC "
					"with pending rx buffers.\n",
					card->name);

				recycle_rx_pool_skb(card, &vc->rcv.rx_pool);
			}
		}
	}
}

static int
open_card_ubr0(struct idt77252_dev *card)
{
	struct vc_map *vc;

	vc = kzalloc(sizeof(struct vc_map), GFP_KERNEL);
	if (!vc) {
		printk("%s: can't alloc vc\n", card->name);
		return -ENOMEM;
	}
	card->vcs[0] = vc;
	vc->class = SCHED_UBR0;

	vc->scq = alloc_scq(card, vc->class);
	if (!vc->scq) {
		printk("%s: can't get SCQ.\n", card->name);
		return -ENOMEM;
	}

	card->scd2vc[0] = vc;
	vc->scd_index = 0;
	vc->scq->scd = card->scd_base;

	fill_scd(card, vc->scq, vc->class);

	write_sram(card, card->tct_base + 0, TCT_UBR | card->scd_base);
	write_sram(card, card->tct_base + 1, 0);
	write_sram(card, card->tct_base + 2, 0);
	write_sram(card, card->tct_base + 3, 0);
	write_sram(card, card->tct_base + 4, 0);
	write_sram(card, card->tct_base + 5, 0);
	write_sram(card, card->tct_base + 6, 0);
	write_sram(card, card->tct_base + 7, TCT_FLAG_UBR);

	clear_bit(VCF_IDLE, &vc->flags);
	writel(TCMDQ_START | 0, SAR_REG_TCMDQ);
	return 0;
}

static int
idt77252_dev_open(struct idt77252_dev *card)
{
	u32 conf;

	if (!test_bit(IDT77252_BIT_INIT, &card->flags)) {
		printk("%s: SAR not yet initialized.\n", card->name);
		return -1;
	}

	conf = SAR_CFG_RXPTH|	/* enable receive path                  */
	    SAR_RX_DELAY |	/* interrupt on complete PDU		*/
	    SAR_CFG_RAWIE |	/* interrupt enable on raw cells        */
	    SAR_CFG_RQFIE |	/* interrupt on RSQ almost full         */
	    SAR_CFG_TMOIE |	/* interrupt on timer overflow          */
	    SAR_CFG_FBIE |	/* interrupt on low free buffers        */
	    SAR_CFG_TXEN |	/* transmit operation enable            */
	    SAR_CFG_TXINT |	/* interrupt on transmit status         */
	    SAR_CFG_TXUIE |	/* interrupt on transmit underrun       */
	    SAR_CFG_TXSFI |	/* interrupt on TSQ almost full         */
	    SAR_CFG_PHYIE	/* enable PHY interrupts		*/
	    ;

#ifdef CONFIG_ATM_IDT77252_RCV_ALL
	/* Test RAW cell receive. */
	conf |= SAR_CFG_VPECA;
#endif

	writel(readl(SAR_REG_CFG) | conf, SAR_REG_CFG);

	if (open_card_oam(card)) {
		printk("%s: Error initializing OAM.\n", card->name);
		return -1;
	}

	if (open_card_ubr0(card)) {
		printk("%s: Error initializing UBR0.\n", card->name);
		return -1;
	}

	IPRINTK("%s: opened IDT77252 ABR SAR.\n", card->name);
	return 0;
}

static void idt77252_dev_close(struct atm_dev *dev)
{
	struct idt77252_dev *card = dev->dev_data;
	u32 conf;

	close_card_oam(card);

	conf = SAR_CFG_RXPTH |	/* enable receive path           */
	    SAR_RX_DELAY |	/* interrupt on complete PDU     */
	    SAR_CFG_RAWIE |	/* interrupt enable on raw cells */
	    SAR_CFG_RQFIE |	/* interrupt on RSQ almost full  */
	    SAR_CFG_TMOIE |	/* interrupt on timer overflow   */
	    SAR_CFG_FBIE |	/* interrupt on low free buffers */
	    SAR_CFG_TXEN |	/* transmit operation enable     */
	    SAR_CFG_TXINT |	/* interrupt on transmit status  */
	    SAR_CFG_TXUIE |	/* interrupt on xmit underrun    */
	    SAR_CFG_TXSFI	/* interrupt on TSQ almost full  */
	    ;

	writel(readl(SAR_REG_CFG) & ~(conf), SAR_REG_CFG);

	DIPRINTK("%s: closed IDT77252 ABR SAR.\n", card->name);
}


/*****************************************************************************/
/*                                                                           */
/* Initialisation and Deinitialization of IDT77252                           */
/*                                                                           */
/*****************************************************************************/


static void
deinit_card(struct idt77252_dev *card)
{
	struct sk_buff *skb;
	int i, j;

	if (!test_bit(IDT77252_BIT_INIT, &card->flags)) {
		printk("%s: SAR not yet initialized.\n", card->name);
		return;
	}
	DIPRINTK("idt77252: deinitialize card %u\n", card->index);

	writel(0, SAR_REG_CFG);

	if (card->atmdev)
		atm_dev_deregister(card->atmdev);

	for (i = 0; i < 4; i++) {
		for (j = 0; j < FBQ_SIZE; j++) {
			skb = card->sbpool[i].skb[j];
			if (skb) {
				pci_unmap_single(card->pcidev,
						 IDT77252_PRV_PADDR(skb),
						 (skb_end_pointer(skb) -
						  skb->data),
						 PCI_DMA_FROMDEVICE);
				card->sbpool[i].skb[j] = NULL;
				dev_kfree_skb(skb);
			}
		}
	}

	vfree(card->soft_tst);

	vfree(card->scd2vc);

	vfree(card->vcs);

	if (card->raw_cell_hnd) {
		pci_free_consistent(card->pcidev, 2 * sizeof(u32),
				    card->raw_cell_hnd, card->raw_cell_paddr);
	}

	if (card->rsq.base) {
		DIPRINTK("%s: Release RSQ ...\n", card->name);
		deinit_rsq(card);
	}

	if (card->tsq.base) {
		DIPRINTK("%s: Release TSQ ...\n", card->name);
		deinit_tsq(card);
	}

	DIPRINTK("idt77252: Release IRQ.\n");
	free_irq(card->pcidev->irq, card);

	for (i = 0; i < 4; i++) {
		if (card->fbq[i])
			iounmap(card->fbq[i]);
	}

	if (card->membase)
		iounmap(card->membase);

	clear_bit(IDT77252_BIT_INIT, &card->flags);
	DIPRINTK("%s: Card deinitialized.\n", card->name);
}


static int __devinit
init_sram(struct idt77252_dev *card)
{
	int i;

	for (i = 0; i < card->sramsize; i += 4)
		write_sram(card, (i >> 2), 0);

	/* set SRAM layout for THIS card */
	if (card->sramsize == (512 * 1024)) {
		card->tct_base = SAR_SRAM_TCT_128_BASE;
		card->tct_size = (SAR_SRAM_TCT_128_TOP - card->tct_base + 1)
		    / SAR_SRAM_TCT_SIZE;
		card->rct_base = SAR_SRAM_RCT_128_BASE;
		card->rct_size = (SAR_SRAM_RCT_128_TOP - card->rct_base + 1)
		    / SAR_SRAM_RCT_SIZE;
		card->rt_base = SAR_SRAM_RT_128_BASE;
		card->scd_base = SAR_SRAM_SCD_128_BASE;
		card->scd_size = (SAR_SRAM_SCD_128_TOP - card->scd_base + 1)
		    / SAR_SRAM_SCD_SIZE;
		card->tst[0] = SAR_SRAM_TST1_128_BASE;
		card->tst[1] = SAR_SRAM_TST2_128_BASE;
		card->tst_size = SAR_SRAM_TST1_128_TOP - card->tst[0] + 1;
		card->abrst_base = SAR_SRAM_ABRSTD_128_BASE;
		card->abrst_size = SAR_ABRSTD_SIZE_8K;
		card->fifo_base = SAR_SRAM_FIFO_128_BASE;
		card->fifo_size = SAR_RXFD_SIZE_32K;
	} else {
		card->tct_base = SAR_SRAM_TCT_32_BASE;
		card->tct_size = (SAR_SRAM_TCT_32_TOP - card->tct_base + 1)
		    / SAR_SRAM_TCT_SIZE;
		card->rct_base = SAR_SRAM_RCT_32_BASE;
		card->rct_size = (SAR_SRAM_RCT_32_TOP - card->rct_base + 1)
		    / SAR_SRAM_RCT_SIZE;
		card->rt_base = SAR_SRAM_RT_32_BASE;
		card->scd_base = SAR_SRAM_SCD_32_BASE;
		card->scd_size = (SAR_SRAM_SCD_32_TOP - card->scd_base + 1)
		    / SAR_SRAM_SCD_SIZE;
		card->tst[0] = SAR_SRAM_TST1_32_BASE;
		card->tst[1] = SAR_SRAM_TST2_32_BASE;
		card->tst_size = (SAR_SRAM_TST1_32_TOP - card->tst[0] + 1);
		card->abrst_base = SAR_SRAM_ABRSTD_32_BASE;
		card->abrst_size = SAR_ABRSTD_SIZE_1K;
		card->fifo_base = SAR_SRAM_FIFO_32_BASE;
		card->fifo_size = SAR_RXFD_SIZE_4K;
	}

	/* Initialize TCT */
	for (i = 0; i < card->tct_size; i++) {
		write_sram(card, i * SAR_SRAM_TCT_SIZE + 0, 0);
		write_sram(card, i * SAR_SRAM_TCT_SIZE + 1, 0);
		write_sram(card, i * SAR_SRAM_TCT_SIZE + 2, 0);
		write_sram(card, i * SAR_SRAM_TCT_SIZE + 3, 0);
		write_sram(card, i * SAR_SRAM_TCT_SIZE + 4, 0);
		write_sram(card, i * SAR_SRAM_TCT_SIZE + 5, 0);
		write_sram(card, i * SAR_SRAM_TCT_SIZE + 6, 0);
		write_sram(card, i * SAR_SRAM_TCT_SIZE + 7, 0);
	}

	/* Initialize RCT */
	for (i = 0; i < card->rct_size; i++) {
		write_sram(card, card->rct_base + i * SAR_SRAM_RCT_SIZE,
				    (u32) SAR_RCTE_RAWCELLINTEN);
		write_sram(card, card->rct_base + i * SAR_SRAM_RCT_SIZE + 1,
				    (u32) 0);
		write_sram(card, card->rct_base + i * SAR_SRAM_RCT_SIZE + 2,
				    (u32) 0);
		write_sram(card, card->rct_base + i * SAR_SRAM_RCT_SIZE + 3,
				    (u32) 0xffffffff);
	}

	writel((SAR_FBQ0_LOW << 28) | 0x00000000 | 0x00000000 |
	       (SAR_FB_SIZE_0 / 48), SAR_REG_FBQS0);
	writel((SAR_FBQ1_LOW << 28) | 0x00000000 | 0x00000000 |
	       (SAR_FB_SIZE_1 / 48), SAR_REG_FBQS1);
	writel((SAR_FBQ2_LOW << 28) | 0x00000000 | 0x00000000 |
	       (SAR_FB_SIZE_2 / 48), SAR_REG_FBQS2);
	writel((SAR_FBQ3_LOW << 28) | 0x00000000 | 0x00000000 |
	       (SAR_FB_SIZE_3 / 48), SAR_REG_FBQS3);

	/* Initialize rate table  */
	for (i = 0; i < 256; i++) {
		write_sram(card, card->rt_base + i, log_to_rate[i]);
	}

	for (i = 0; i < 128; i++) {
		unsigned int tmp;

		tmp  = rate_to_log[(i << 2) + 0] << 0;
		tmp |= rate_to_log[(i << 2) + 1] << 8;
		tmp |= rate_to_log[(i << 2) + 2] << 16;
		tmp |= rate_to_log[(i << 2) + 3] << 24;
		write_sram(card, card->rt_base + 256 + i, tmp);
	}

#if 0 /* Fill RDF and AIR tables. */
	for (i = 0; i < 128; i++) {
		unsigned int tmp;

		tmp = RDF[0][(i << 1) + 0] << 16;
		tmp |= RDF[0][(i << 1) + 1] << 0;
		write_sram(card, card->rt_base + 512 + i, tmp);
	}

	for (i = 0; i < 128; i++) {
		unsigned int tmp;

		tmp = AIR[0][(i << 1) + 0] << 16;
		tmp |= AIR[0][(i << 1) + 1] << 0;
		write_sram(card, card->rt_base + 640 + i, tmp);
	}
#endif

	IPRINTK("%s: initialize rate table ...\n", card->name);
	writel(card->rt_base << 2, SAR_REG_RTBL);

	/* Initialize TSTs */
	IPRINTK("%s: initialize TST ...\n", card->name);
	card->tst_free = card->tst_size - 2;	/* last two are jumps */

	for (i = card->tst[0]; i < card->tst[0] + card->tst_size - 2; i++)
		write_sram(card, i, TSTE_OPC_VAR);
	write_sram(card, i++, TSTE_OPC_JMP | (card->tst[0] << 2));
	idt77252_sram_write_errors = 1;
	write_sram(card, i++, TSTE_OPC_JMP | (card->tst[1] << 2));
	idt77252_sram_write_errors = 0;
	for (i = card->tst[1]; i < card->tst[1] + card->tst_size - 2; i++)
		write_sram(card, i, TSTE_OPC_VAR);
	write_sram(card, i++, TSTE_OPC_JMP | (card->tst[1] << 2));
	idt77252_sram_write_errors = 1;
	write_sram(card, i++, TSTE_OPC_JMP | (card->tst[0] << 2));
	idt77252_sram_write_errors = 0;

	card->tst_index = 0;
	writel(card->tst[0] << 2, SAR_REG_TSTB);

	/* Initialize ABRSTD and Receive FIFO */
	IPRINTK("%s: initialize ABRSTD ...\n", card->name);
	writel(card->abrst_size | (card->abrst_base << 2),
	       SAR_REG_ABRSTD);

	IPRINTK("%s: initialize receive fifo ...\n", card->name);
	writel(card->fifo_size | (card->fifo_base << 2),
	       SAR_REG_RXFD);

	IPRINTK("%s: SRAM initialization complete.\n", card->name);
	return 0;
}

static int __devinit
init_card(struct atm_dev *dev)
{
	struct idt77252_dev *card = dev->dev_data;
	struct pci_dev *pcidev = card->pcidev;
	unsigned long tmpl, modl;
	unsigned int linkrate, rsvdcr;
	unsigned int tst_entries;
	struct net_device *tmp;
	char tname[10];

	u32 size;
	u_char pci_byte;
	u32 conf;
	int i, k;

	if (test_bit(IDT77252_BIT_INIT, &card->flags)) {
		printk("Error: SAR already initialized.\n");
		return -1;
	}

/*****************************************************************/
/*   P C I   C O N F I G U R A T I O N                           */
/*****************************************************************/

	/* Set PCI Retry-Timeout and TRDY timeout */
	IPRINTK("%s: Checking PCI retries.\n", card->name);
	if (pci_read_config_byte(pcidev, 0x40, &pci_byte) != 0) {
		printk("%s: can't read PCI retry timeout.\n", card->name);
		deinit_card(card);
		return -1;
	}
	if (pci_byte != 0) {
		IPRINTK("%s: PCI retry timeout: %d, set to 0.\n",
			card->name, pci_byte);
		if (pci_write_config_byte(pcidev, 0x40, 0) != 0) {
			printk("%s: can't set PCI retry timeout.\n",
			       card->name);
			deinit_card(card);
			return -1;
		}
	}
	IPRINTK("%s: Checking PCI TRDY.\n", card->name);
	if (pci_read_config_byte(pcidev, 0x41, &pci_byte) != 0) {
		printk("%s: can't read PCI TRDY timeout.\n", card->name);
		deinit_card(card);
		return -1;
	}
	if (pci_byte != 0) {
		IPRINTK("%s: PCI TRDY timeout: %d, set to 0.\n",
		        card->name, pci_byte);
		if (pci_write_config_byte(pcidev, 0x41, 0) != 0) {
			printk("%s: can't set PCI TRDY timeout.\n", card->name);
			deinit_card(card);
			return -1;
		}
	}
	/* Reset Timer register */
	if (readl(SAR_REG_STAT) & SAR_STAT_TMROF) {
		printk("%s: resetting timer overflow.\n", card->name);
		writel(SAR_STAT_TMROF, SAR_REG_STAT);
	}
	IPRINTK("%s: Request IRQ ... ", card->name);
	if (request_irq(pcidev->irq, idt77252_interrupt, IRQF_SHARED,
			card->name, card) != 0) {
		printk("%s: can't allocate IRQ.\n", card->name);
		deinit_card(card);
		return -1;
	}
	IPRINTK("got %d.\n", pcidev->irq);

/*****************************************************************/
/*   C H E C K   A N D   I N I T   S R A M                       */
/*****************************************************************/

	IPRINTK("%s: Initializing SRAM\n", card->name);

	/* preset size of connecton table, so that init_sram() knows about it */
	conf =	SAR_CFG_TX_FIFO_SIZE_9 |	/* Use maximum fifo size */
		SAR_CFG_RXSTQ_SIZE_8k |		/* Receive Status Queue is 8k */
		SAR_CFG_IDLE_CLP |		/* Set CLP on idle cells */
#ifndef ATM_IDT77252_SEND_IDLE
		SAR_CFG_NO_IDLE |		/* Do not send idle cells */
#endif
		0;

	if (card->sramsize == (512 * 1024))
		conf |= SAR_CFG_CNTBL_1k;
	else
		conf |= SAR_CFG_CNTBL_512;

	switch (vpibits) {
	case 0:
		conf |= SAR_CFG_VPVCS_0;
		break;
	default:
	case 1:
		conf |= SAR_CFG_VPVCS_1;
		break;
	case 2:
		conf |= SAR_CFG_VPVCS_2;
		break;
	case 8:
		conf |= SAR_CFG_VPVCS_8;
		break;
	}

	writel(readl(SAR_REG_CFG) | conf, SAR_REG_CFG);

	if (init_sram(card) < 0)
		return -1;

/********************************************************************/
/*  A L L O C   R A M   A N D   S E T   V A R I O U S   T H I N G S */
/********************************************************************/
	/* Initialize TSQ */
	if (0 != init_tsq(card)) {
		deinit_card(card);
		return -1;
	}
	/* Initialize RSQ */
	if (0 != init_rsq(card)) {
		deinit_card(card);
		return -1;
	}

	card->vpibits = vpibits;
	if (card->sramsize == (512 * 1024)) {
		card->vcibits = 10 - card->vpibits;
	} else {
		card->vcibits = 9 - card->vpibits;
	}

	card->vcimask = 0;
	for (k = 0, i = 1; k < card->vcibits; k++) {
		card->vcimask |= i;
		i <<= 1;
	}

	IPRINTK("%s: Setting VPI/VCI mask to zero.\n", card->name);
	writel(0, SAR_REG_VPM);

	/* Little Endian Order   */
	writel(0, SAR_REG_GP);

	/* Initialize RAW Cell Handle Register  */
	card->raw_cell_hnd = pci_alloc_consistent(card->pcidev, 2 * sizeof(u32),
						  &card->raw_cell_paddr);
	if (!card->raw_cell_hnd) {
		printk("%s: memory allocation failure.\n", card->name);
		deinit_card(card);
		return -1;
	}
	memset(card->raw_cell_hnd, 0, 2 * sizeof(u32));
	writel(card->raw_cell_paddr, SAR_REG_RAWHND);
	IPRINTK("%s: raw cell handle is at 0x%p.\n", card->name,
		card->raw_cell_hnd);

	size = sizeof(struct vc_map *) * card->tct_size;
	IPRINTK("%s: allocate %d byte for VC map.\n", card->name, size);
	if (NULL == (card->vcs = vmalloc(size))) {
		printk("%s: memory allocation failure.\n", card->name);
		deinit_card(card);
		return -1;
	}
	memset(card->vcs, 0, size);

	size = sizeof(struct vc_map *) * card->scd_size;
	IPRINTK("%s: allocate %d byte for SCD to VC mapping.\n",
	        card->name, size);
	if (NULL == (card->scd2vc = vmalloc(size))) {
		printk("%s: memory allocation failure.\n", card->name);
		deinit_card(card);
		return -1;
	}
	memset(card->scd2vc, 0, size);

	size = sizeof(struct tst_info) * (card->tst_size - 2);
	IPRINTK("%s: allocate %d byte for TST to VC mapping.\n",
		card->name, size);
	if (NULL == (card->soft_tst = vmalloc(size))) {
		printk("%s: memory allocation failure.\n", card->name);
		deinit_card(card);
		return -1;
	}
	for (i = 0; i < card->tst_size - 2; i++) {
		card->soft_tst[i].tste = TSTE_OPC_VAR;
		card->soft_tst[i].vc = NULL;
	}

	if (dev->phy == NULL) {
		printk("%s: No LT device defined.\n", card->name);
		deinit_card(card);
		return -1;
	}
	if (dev->phy->ioctl == NULL) {
		printk("%s: LT had no IOCTL funtion defined.\n", card->name);
		deinit_card(card);
		return -1;
	}

#ifdef	CONFIG_ATM_IDT77252_USE_SUNI
	/*
	 * this is a jhs hack to get around special functionality in the
	 * phy driver for the atecom hardware; the functionality doesn't
	 * exist in the linux atm suni driver
	 *
	 * it isn't the right way to do things, but as the guy from NIST
	 * said, talking about their measurement of the fine structure
	 * constant, "it's good enough for government work."
	 */
	linkrate = 149760000;
#endif

	card->link_pcr = (linkrate / 8 / 53);
	printk("%s: Linkrate on ATM line : %u bit/s, %u cell/s.\n",
	       card->name, linkrate, card->link_pcr);

#ifdef ATM_IDT77252_SEND_IDLE
	card->utopia_pcr = card->link_pcr;
#else
	card->utopia_pcr = (160000000 / 8 / 54);
#endif

	rsvdcr = 0;
	if (card->utopia_pcr > card->link_pcr)
		rsvdcr = card->utopia_pcr - card->link_pcr;

	tmpl = (unsigned long) rsvdcr * ((unsigned long) card->tst_size - 2);
	modl = tmpl % (unsigned long)card->utopia_pcr;
	tst_entries = (int) (tmpl / (unsigned long)card->utopia_pcr);
	if (modl)
		tst_entries++;
	card->tst_free -= tst_entries;
	fill_tst(card, NULL, tst_entries, TSTE_OPC_NULL);

#ifdef HAVE_EEPROM
	idt77252_eeprom_init(card);
	printk("%s: EEPROM: %02x:", card->name,
		idt77252_eeprom_read_status(card));

	for (i = 0; i < 0x80; i++) {
		printk(" %02x", 
		idt77252_eeprom_read_byte(card, i)
		);
	}
	printk("\n");
#endif /* HAVE_EEPROM */

	/*
	 * XXX: <hack>
	 */
	sprintf(tname, "eth%d", card->index);
	tmp = dev_get_by_name(&init_net, tname);	/* jhs: was "tmp = dev_get(tname);" */
	if (tmp) {
		memcpy(card->atmdev->esi, tmp->dev_addr, 6);

		printk("%s: ESI %pM\n", card->name, card->atmdev->esi);
	}
	/*
	 * XXX: </hack>
	 */

	/* Set Maximum Deficit Count for now. */
	writel(0xffff, SAR_REG_MDFCT);

	set_bit(IDT77252_BIT_INIT, &card->flags);

	XPRINTK("%s: IDT77252 ABR SAR initialization complete.\n", card->name);
	return 0;
}


/*****************************************************************************/
/*                                                                           */
/* Probing of IDT77252 ABR SAR                                               */
/*                                                                           */
/*****************************************************************************/


static int __devinit
idt77252_preset(struct idt77252_dev *card)
{
	u16 pci_command;

/*****************************************************************/
/*   P C I   C O N F I G U R A T I O N                           */
/*****************************************************************/

	XPRINTK("%s: Enable PCI master and memory access for SAR.\n",
		card->name);
	if (pci_read_config_word(card->pcidev, PCI_COMMAND, &pci_command)) {
		printk("%s: can't read PCI_COMMAND.\n", card->name);
		deinit_card(card);
		return -1;
	}
	if (!(pci_command & PCI_COMMAND_IO)) {
		printk("%s: PCI_COMMAND: %04x (???)\n",
		       card->name, pci_command);
		deinit_card(card);
		return (-1);
	}
	pci_command |= (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
	if (pci_write_config_word(card->pcidev, PCI_COMMAND, pci_command)) {
		printk("%s: can't write PCI_COMMAND.\n", card->name);
		deinit_card(card);
		return -1;
	}
/*****************************************************************/
/*   G E N E R I C   R E S E T                                   */
/*****************************************************************/

	/* Software reset */
	writel(SAR_CFG_SWRST, SAR_REG_CFG);
	mdelay(1);
	writel(0, SAR_REG_CFG);

	IPRINTK("%s: Software resetted.\n", card->name);
	return 0;
}


static unsigned long __devinit
probe_sram(struct idt77252_dev *card)
{
	u32 data, addr;

	writel(0, SAR_REG_DR0);
	writel(SAR_CMD_WRITE_SRAM | (0 << 2), SAR_REG_CMD);

	for (addr = 0x4000; addr < 0x80000; addr += 0x4000) {
		writel(ATM_POISON, SAR_REG_DR0);
		writel(SAR_CMD_WRITE_SRAM | (addr << 2), SAR_REG_CMD);

		writel(SAR_CMD_READ_SRAM | (0 << 2), SAR_REG_CMD);
		data = readl(SAR_REG_DR0);

		if (data != 0)
			break;
	}

	return addr * sizeof(u32);
}

static int __devinit
idt77252_init_one(struct pci_dev *pcidev, const struct pci_device_id *id)
{
	static struct idt77252_dev **last = &idt77252_chain;
	static int index = 0;

	unsigned long membase, srambase;
	struct idt77252_dev *card;
	struct atm_dev *dev;
	int i, err;


	if ((err = pci_enable_device(pcidev))) {
		printk("idt77252: can't enable PCI device at %s\n", pci_name(pcidev));
		return err;
	}

	card = kzalloc(sizeof(struct idt77252_dev), GFP_KERNEL);
	if (!card) {
		printk("idt77252-%d: can't allocate private data\n", index);
		err = -ENOMEM;
		goto err_out_disable_pdev;
	}
	card->revision = pcidev->revision;
	card->index = index;
	card->pcidev = pcidev;
	sprintf(card->name, "idt77252-%d", card->index);

	INIT_WORK(&card->tqueue, idt77252_softint);

	membase = pci_resource_start(pcidev, 1);
	srambase = pci_resource_start(pcidev, 2);

	mutex_init(&card->mutex);
	spin_lock_init(&card->cmd_lock);
	spin_lock_init(&card->tst_lock);

	init_timer(&card->tst_timer);
	card->tst_timer.data = (unsigned long)card;
	card->tst_timer.function = tst_timer;

	/* Do the I/O remapping... */
	card->membase = ioremap(membase, 1024);
	if (!card->membase) {
		printk("%s: can't ioremap() membase\n", card->name);
		err = -EIO;
		goto err_out_free_card;
	}

	if (idt77252_preset(card)) {
		printk("%s: preset failed\n", card->name);
		err = -EIO;
		goto err_out_iounmap;
	}

	dev = atm_dev_register("idt77252", &idt77252_ops, -1, NULL);
	if (!dev) {
		printk("%s: can't register atm device\n", card->name);
		err = -EIO;
		goto err_out_iounmap;
	}
	dev->dev_data = card;
	card->atmdev = dev;

#ifdef	CONFIG_ATM_IDT77252_USE_SUNI
	suni_init(dev);
	if (!dev->phy) {
		printk("%s: can't init SUNI\n", card->name);
		err = -EIO;
		goto err_out_deinit_card;
	}
#endif	/* CONFIG_ATM_IDT77252_USE_SUNI */

	card->sramsize = probe_sram(card);

	for (i = 0; i < 4; i++) {
		card->fbq[i] = ioremap(srambase | 0x200000 | (i << 18), 4);
		if (!card->fbq[i]) {
			printk("%s: can't ioremap() FBQ%d\n", card->name, i);
			err = -EIO;
			goto err_out_deinit_card;
		}
	}

	printk("%s: ABR SAR (Rev %c): MEM %08lx SRAM %08lx [%u KB]\n",
	       card->name, ((card->revision > 1) && (card->revision < 25)) ?
	       'A' + card->revision - 1 : '?', membase, srambase,
	       card->sramsize / 1024);

	if (init_card(dev)) {
		printk("%s: init_card failed\n", card->name);
		err = -EIO;
		goto err_out_deinit_card;
	}

	dev->ci_range.vpi_bits = card->vpibits;
	dev->ci_range.vci_bits = card->vcibits;
	dev->link_rate = card->link_pcr;

	if (dev->phy->start)
		dev->phy->start(dev);

	if (idt77252_dev_open(card)) {
		printk("%s: dev_open failed\n", card->name);
		err = -EIO;
		goto err_out_stop;
	}

	*last = card;
	last = &card->next;
	index++;

	return 0;

err_out_stop:
	if (dev->phy->stop)
		dev->phy->stop(dev);

err_out_deinit_card:
	deinit_card(card);

err_out_iounmap:
	iounmap(card->membase);

err_out_free_card:
	kfree(card);

err_out_disable_pdev:
	pci_disable_device(pcidev);
	return err;
}

static struct pci_device_id idt77252_pci_tbl[] =
{
	{ PCI_VDEVICE(IDT, PCI_DEVICE_ID_IDT_IDT77252), 0 },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, idt77252_pci_tbl);

static struct pci_driver idt77252_driver = {
	.name		= "idt77252",
	.id_table	= idt77252_pci_tbl,
	.probe		= idt77252_init_one,
};

static int __init idt77252_init(void)
{
	struct sk_buff *skb;

	printk("%s: at %p\n", __func__, idt77252_init);

	if (sizeof(skb->cb) < sizeof(struct atm_skb_data) +
			      sizeof(struct idt77252_skb_prv)) {
		printk(KERN_ERR "%s: skb->cb is too small (%lu < %lu)\n",
		       __func__, (unsigned long) sizeof(skb->cb),
		       (unsigned long) sizeof(struct atm_skb_data) +
				       sizeof(struct idt77252_skb_prv));
		return -EIO;
	}

	return pci_register_driver(&idt77252_driver);
}

static void __exit idt77252_exit(void)
{
	struct idt77252_dev *card;
	struct atm_dev *dev;

	pci_unregister_driver(&idt77252_driver);

	while (idt77252_chain) {
		card = idt77252_chain;
		dev = card->atmdev;
		idt77252_chain = card->next;

		if (dev->phy->stop)
			dev->phy->stop(dev);
		deinit_card(card);
		pci_disable_device(card->pcidev);
		kfree(card);
	}

	DIPRINTK("idt77252: finished cleanup-module().\n");
}

module_init(idt77252_init);
module_exit(idt77252_exit);

MODULE_LICENSE("GPL");

module_param(vpibits, uint, 0);
MODULE_PARM_DESC(vpibits, "number of VPI bits supported (0, 1, or 2)");
#ifdef CONFIG_ATM_IDT77252_DEBUG
module_param(debug, ulong, 0644);
MODULE_PARM_DESC(debug,   "debug bitmap, see drivers/atm/idt77252.h");
#endif

MODULE_AUTHOR("Eddie C. Dost <ecd@atecom.com>");
MODULE_DESCRIPTION("IDT77252 ABR SAR Driver");

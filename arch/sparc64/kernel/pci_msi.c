/* pci_msi.c: Sparc64 MSI support common layer.
 *
 * Copyright (C) 2007 David S. Miller (davem@davemloft.net)
 */
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include "pci_impl.h"

static irqreturn_t sparc64_msiq_interrupt(int irq, void *cookie)
{
	struct sparc64_msiq_cookie *msiq_cookie = cookie;
	struct pci_pbm_info *pbm = msiq_cookie->pbm;
	unsigned long msiqid = msiq_cookie->msiqid;
	const struct sparc64_msiq_ops *ops;
	unsigned long orig_head, head;
	int err;

	ops = pbm->msi_ops;

	err = ops->get_head(pbm, msiqid, &head);
	if (unlikely(err < 0))
		goto err_get_head;

	orig_head = head;
	for (;;) {
		unsigned long msi;

		err = ops->dequeue_msi(pbm, msiqid, &head, &msi);
		if (likely(err > 0)) {
			struct irq_desc *desc;
			unsigned int virt_irq;

			virt_irq = pbm->msi_irq_table[msi - pbm->msi_first];
			desc = irq_desc + virt_irq;

			desc->handle_irq(virt_irq, desc);
		}

		if (unlikely(err < 0))
			goto err_dequeue;

		if (err == 0)
			break;
	}
	if (likely(head != orig_head)) {
		err = ops->set_head(pbm, msiqid, head);
		if (unlikely(err < 0))
			goto err_set_head;
	}
	return IRQ_HANDLED;

err_get_head:
	printk(KERN_EMERG "MSI: Get head on msiqid[%lu] gives error %d\n",
	       msiqid, err);
	goto err_out;

err_dequeue:
	printk(KERN_EMERG "MSI: Dequeue head[%lu] from msiqid[%lu] "
	       "gives error %d\n",
	       head, msiqid, err);
	goto err_out;

err_set_head:
	printk(KERN_EMERG "MSI: Set head[%lu] on msiqid[%lu] "
	       "gives error %d\n",
	       head, msiqid, err);
	goto err_out;

err_out:
	return IRQ_NONE;
}

static u32 pick_msiq(struct pci_pbm_info *pbm)
{
	static DEFINE_SPINLOCK(rotor_lock);
	unsigned long flags;
	u32 ret, rotor;

	spin_lock_irqsave(&rotor_lock, flags);

	rotor = pbm->msiq_rotor;
	ret = pbm->msiq_first + rotor;

	if (++rotor >= pbm->msiq_num)
		rotor = 0;
	pbm->msiq_rotor = rotor;

	spin_unlock_irqrestore(&rotor_lock, flags);

	return ret;
}


static int alloc_msi(struct pci_pbm_info *pbm)
{
	int i;

	for (i = 0; i < pbm->msi_num; i++) {
		if (!test_and_set_bit(i, pbm->msi_bitmap))
			return i + pbm->msi_first;
	}

	return -ENOENT;
}

static void free_msi(struct pci_pbm_info *pbm, int msi_num)
{
	msi_num -= pbm->msi_first;
	clear_bit(msi_num, pbm->msi_bitmap);
}

static struct irq_chip msi_irq = {
	.typename	= "PCI-MSI",
	.mask		= mask_msi_irq,
	.unmask		= unmask_msi_irq,
	.enable		= unmask_msi_irq,
	.disable	= mask_msi_irq,
	/* XXX affinity XXX */
};

int sparc64_setup_msi_irq(unsigned int *virt_irq_p,
			  struct pci_dev *pdev,
			  struct msi_desc *entry)
{
	struct pci_pbm_info *pbm = pdev->dev.archdata.host_controller;
	const struct sparc64_msiq_ops *ops = pbm->msi_ops;
	struct msi_msg msg;
	int msi, err;
	u32 msiqid;

	*virt_irq_p = virt_irq_alloc(0, 0);
	err = -ENOMEM;
	if (!*virt_irq_p)
		goto out_err;

	set_irq_chip_and_handler_name(*virt_irq_p, &msi_irq,
				      handle_simple_irq, "MSI");

	err = alloc_msi(pbm);
	if (unlikely(err < 0))
		goto out_virt_irq_free;

	msi = err;

	msiqid = pick_msiq(pbm);

	err = ops->msi_setup(pbm, msiqid, msi,
			     (entry->msi_attrib.is_64 ? 1 : 0));
	if (err)
		goto out_msi_free;

	pbm->msi_irq_table[msi - pbm->msi_first] = *virt_irq_p;

	if (entry->msi_attrib.is_64) {
		msg.address_hi = pbm->msi64_start >> 32;
		msg.address_lo = pbm->msi64_start & 0xffffffff;
	} else {
		msg.address_hi = 0;
		msg.address_lo = pbm->msi32_start;
	}
	msg.data = msi;

	set_irq_msi(*virt_irq_p, entry);
	write_msi_msg(*virt_irq_p, &msg);

	return 0;

out_msi_free:
	free_msi(pbm, msi);

out_virt_irq_free:
	set_irq_chip(*virt_irq_p, NULL);
	virt_irq_free(*virt_irq_p);
	*virt_irq_p = 0;

out_err:
	return err;
}

void sparc64_teardown_msi_irq(unsigned int virt_irq,
			      struct pci_dev *pdev)
{
	struct pci_pbm_info *pbm = pdev->dev.archdata.host_controller;
	const struct sparc64_msiq_ops *ops = pbm->msi_ops;
	unsigned int msi_num;
	int i, err;

	for (i = 0; i < pbm->msi_num; i++) {
		if (pbm->msi_irq_table[i] == virt_irq)
			break;
	}
	if (i >= pbm->msi_num) {
		printk(KERN_ERR "%s: teardown: No MSI for irq %u\n",
		       pbm->name, virt_irq);
		return;
	}

	msi_num = pbm->msi_first + i;
	pbm->msi_irq_table[i] = ~0U;

	err = ops->msi_teardown(pbm, msi_num);
	if (err) {
		printk(KERN_ERR "%s: teardown: ops->teardown() on MSI %u, "
		       "irq %u, gives error %d\n",
		       pbm->name, msi_num, virt_irq, err);
		return;
	}

	free_msi(pbm, msi_num);

	set_irq_chip(virt_irq, NULL);
	virt_irq_free(virt_irq);
}

static int msi_bitmap_alloc(struct pci_pbm_info *pbm)
{
	unsigned long size, bits_per_ulong;

	bits_per_ulong = sizeof(unsigned long) * 8;
	size = (pbm->msi_num + (bits_per_ulong - 1)) & ~(bits_per_ulong - 1);
	size /= 8;
	BUG_ON(size % sizeof(unsigned long));

	pbm->msi_bitmap = kzalloc(size, GFP_KERNEL);
	if (!pbm->msi_bitmap)
		return -ENOMEM;

	return 0;
}

static void msi_bitmap_free(struct pci_pbm_info *pbm)
{
	kfree(pbm->msi_bitmap);
	pbm->msi_bitmap = NULL;
}

static int msi_table_alloc(struct pci_pbm_info *pbm)
{
	int size, i;

	size = pbm->msiq_num * sizeof(struct sparc64_msiq_cookie);
	pbm->msiq_irq_cookies = kzalloc(size, GFP_KERNEL);
	if (!pbm->msiq_irq_cookies)
		return -ENOMEM;

	for (i = 0; i < pbm->msiq_num; i++) {
		struct sparc64_msiq_cookie *p;

		p = &pbm->msiq_irq_cookies[i];
		p->pbm = pbm;
		p->msiqid = pbm->msiq_first + i;
	}

	size = pbm->msi_num * sizeof(unsigned int);
	pbm->msi_irq_table = kzalloc(size, GFP_KERNEL);
	if (!pbm->msi_irq_table) {
		kfree(pbm->msiq_irq_cookies);
		pbm->msiq_irq_cookies = NULL;
		return -ENOMEM;
	}

	return 0;
}

static void msi_table_free(struct pci_pbm_info *pbm)
{
	kfree(pbm->msiq_irq_cookies);
	pbm->msiq_irq_cookies = NULL;

	kfree(pbm->msi_irq_table);
	pbm->msi_irq_table = NULL;
}

static int bringup_one_msi_queue(struct pci_pbm_info *pbm,
				 const struct sparc64_msiq_ops *ops,
				 unsigned long msiqid,
				 unsigned long devino)
{
	int irq = ops->msiq_build_irq(pbm, msiqid, devino);
	int err, nid;

	if (irq < 0)
		return irq;

	nid = pbm->numa_node;
	if (nid != -1) {
		cpumask_t numa_mask = node_to_cpumask(nid);

		irq_set_affinity(irq, numa_mask);
	}
	err = request_irq(irq, sparc64_msiq_interrupt, 0,
			  "MSIQ",
			  &pbm->msiq_irq_cookies[msiqid - pbm->msiq_first]);
	if (err)
		return err;

	return 0;
}

static int sparc64_bringup_msi_queues(struct pci_pbm_info *pbm,
				      const struct sparc64_msiq_ops *ops)
{
	int i;

	for (i = 0; i < pbm->msiq_num; i++) {
		unsigned long msiqid = i + pbm->msiq_first;
		unsigned long devino = i + pbm->msiq_first_devino;
		int err;

		err = bringup_one_msi_queue(pbm, ops, msiqid, devino);
		if (err)
			return err;
	}

	return 0;
}

void sparc64_pbm_msi_init(struct pci_pbm_info *pbm,
			  const struct sparc64_msiq_ops *ops)
{
	const u32 *val;
	int len;

	val = of_get_property(pbm->prom_node, "#msi-eqs", &len);
	if (!val || len != 4)
		goto no_msi;
	pbm->msiq_num = *val;
	if (pbm->msiq_num) {
		const struct msiq_prop {
			u32 first_msiq;
			u32 num_msiq;
			u32 first_devino;
		} *mqp;
		const struct msi_range_prop {
			u32 first_msi;
			u32 num_msi;
		} *mrng;
		const struct addr_range_prop {
			u32 msi32_high;
			u32 msi32_low;
			u32 msi32_len;
			u32 msi64_high;
			u32 msi64_low;
			u32 msi64_len;
		} *arng;

		val = of_get_property(pbm->prom_node, "msi-eq-size", &len);
		if (!val || len != 4)
			goto no_msi;

		pbm->msiq_ent_count = *val;

		mqp = of_get_property(pbm->prom_node,
				      "msi-eq-to-devino", &len);
		if (!mqp)
			mqp = of_get_property(pbm->prom_node,
					      "msi-eq-devino", &len);
		if (!mqp || len != sizeof(struct msiq_prop))
			goto no_msi;

		pbm->msiq_first = mqp->first_msiq;
		pbm->msiq_first_devino = mqp->first_devino;

		val = of_get_property(pbm->prom_node, "#msi", &len);
		if (!val || len != 4)
			goto no_msi;
		pbm->msi_num = *val;

		mrng = of_get_property(pbm->prom_node, "msi-ranges", &len);
		if (!mrng || len != sizeof(struct msi_range_prop))
			goto no_msi;
		pbm->msi_first = mrng->first_msi;

		val = of_get_property(pbm->prom_node, "msi-data-mask", &len);
		if (!val || len != 4)
			goto no_msi;
		pbm->msi_data_mask = *val;

		val = of_get_property(pbm->prom_node, "msix-data-width", &len);
		if (!val || len != 4)
			goto no_msi;
		pbm->msix_data_width = *val;

		arng = of_get_property(pbm->prom_node, "msi-address-ranges",
				       &len);
		if (!arng || len != sizeof(struct addr_range_prop))
			goto no_msi;
		pbm->msi32_start = ((u64)arng->msi32_high << 32) |
			(u64) arng->msi32_low;
		pbm->msi64_start = ((u64)arng->msi64_high << 32) |
			(u64) arng->msi64_low;
		pbm->msi32_len = arng->msi32_len;
		pbm->msi64_len = arng->msi64_len;

		if (msi_bitmap_alloc(pbm))
			goto no_msi;

		if (msi_table_alloc(pbm)) {
			msi_bitmap_free(pbm);
			goto no_msi;
		}

		if (ops->msiq_alloc(pbm)) {
			msi_table_free(pbm);
			msi_bitmap_free(pbm);
			goto no_msi;
		}

		if (sparc64_bringup_msi_queues(pbm, ops)) {
			ops->msiq_free(pbm);
			msi_table_free(pbm);
			msi_bitmap_free(pbm);
			goto no_msi;
		}

		printk(KERN_INFO "%s: MSI Queue first[%u] num[%u] count[%u] "
		       "devino[0x%x]\n",
		       pbm->name,
		       pbm->msiq_first, pbm->msiq_num,
		       pbm->msiq_ent_count,
		       pbm->msiq_first_devino);
		printk(KERN_INFO "%s: MSI first[%u] num[%u] mask[0x%x] "
		       "width[%u]\n",
		       pbm->name,
		       pbm->msi_first, pbm->msi_num, pbm->msi_data_mask,
		       pbm->msix_data_width);
		printk(KERN_INFO "%s: MSI addr32[0x%lx:0x%x] "
		       "addr64[0x%lx:0x%x]\n",
		       pbm->name,
		       pbm->msi32_start, pbm->msi32_len,
		       pbm->msi64_start, pbm->msi64_len);
		printk(KERN_INFO "%s: MSI queues at RA [%016lx]\n",
		       pbm->name,
		       __pa(pbm->msi_queues));

		pbm->msi_ops = ops;
		pbm->setup_msi_irq = sparc64_setup_msi_irq;
		pbm->teardown_msi_irq = sparc64_teardown_msi_irq;
	}
	return;

no_msi:
	pbm->msiq_num = 0;
	printk(KERN_INFO "%s: No MSI support.\n", pbm->name);
}

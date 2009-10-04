/*
 * devoard misc stuff.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

/* register a pcmcia socket */
int __init db1x_register_pcmcia_socket(unsigned long pseudo_attr_start,
				       unsigned long pseudo_attr_end,
				       unsigned long pseudo_mem_start,
				       unsigned long pseudo_mem_end,
				       unsigned long pseudo_io_start,
				       unsigned long pseudo_io_end,
				       int card_irq,
				       int cd_irq,
				       int stschg_irq,
				       int eject_irq,
				       int id)
{
	int cnt, i, ret;
	struct resource *sr;
	struct platform_device *pd;

	cnt = 5;
	if (eject_irq)
		cnt++;
	if (stschg_irq)
		cnt++;

	sr = kzalloc(sizeof(struct resource) * cnt, GFP_KERNEL);
	if (!sr)
		return -ENOMEM;

	pd = platform_device_alloc("db1xxx_pcmcia", id);
	if (!pd) {
		ret = -ENOMEM;
		goto out;
	}

	sr[0].name	= "pseudo-attr";
	sr[0].flags	= IORESOURCE_MEM;
	sr[0].start	= pseudo_attr_start;
	sr[0].end	= pseudo_attr_end;

	sr[1].name	= "pseudo-mem";
	sr[1].flags	= IORESOURCE_MEM;
	sr[1].start	= pseudo_mem_start;
	sr[1].end	= pseudo_mem_end;

	sr[2].name	= "pseudo-io";
	sr[2].flags	= IORESOURCE_MEM;
	sr[2].start	= pseudo_io_start;
	sr[2].end	= pseudo_io_end;

	sr[3].name	= "insert";
	sr[3].flags	= IORESOURCE_IRQ;
	sr[3].start = sr[3].end = cd_irq;

	sr[4].name	= "card";
	sr[4].flags	= IORESOURCE_IRQ;
	sr[4].start = sr[4].end = card_irq;

	i = 5;
	if (stschg_irq) {
		sr[i].name	= "insert";
		sr[i].flags	= IORESOURCE_IRQ;
		sr[i].start = sr[i].end = cd_irq;
		i++;
	}
	if (eject_irq) {
		sr[i].name	= "eject";
		sr[i].flags	= IORESOURCE_IRQ;
		sr[i].start = sr[i].end = eject_irq;
	}

	pd->resource = sr;
	pd->num_resources = cnt;

	ret = platform_device_add(pd);
	if (!ret)
		return 0;

	platform_device_put(pd);
out:
	kfree(sr);
	return ret;
}

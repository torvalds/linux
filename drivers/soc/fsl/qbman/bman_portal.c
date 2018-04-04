/* Copyright 2008 - 2016 Freescale Semiconductor, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *	 notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *	 notice, this list of conditions and the following disclaimer in the
 *	 documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *	 names of its contributors may be used to endorse or promote products
 *	 derived from this software without specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "bman_priv.h"

static struct bman_portal *affine_bportals[NR_CPUS];
static struct cpumask portal_cpus;
/* protect bman global registers and global data shared among portals */
static DEFINE_SPINLOCK(bman_lock);

static struct bman_portal *init_pcfg(struct bm_portal_config *pcfg)
{
	struct bman_portal *p = bman_create_affine_portal(pcfg);

	if (!p) {
		dev_crit(pcfg->dev, "%s: Portal failure on cpu %d\n",
			 __func__, pcfg->cpu);
		return NULL;
	}

	bman_p_irqsource_add(p, BM_PIRQ_RCRI);
	affine_bportals[pcfg->cpu] = p;

	dev_info(pcfg->dev, "Portal initialised, cpu %d\n", pcfg->cpu);

	return p;
}

static int bman_offline_cpu(unsigned int cpu)
{
	struct bman_portal *p = affine_bportals[cpu];
	const struct bm_portal_config *pcfg;

	if (!p)
		return 0;

	pcfg = bman_get_bm_portal_config(p);
	if (!pcfg)
		return 0;

	irq_set_affinity(pcfg->irq, cpumask_of(0));
	return 0;
}

static int bman_online_cpu(unsigned int cpu)
{
	struct bman_portal *p = affine_bportals[cpu];
	const struct bm_portal_config *pcfg;

	if (!p)
		return 0;

	pcfg = bman_get_bm_portal_config(p);
	if (!pcfg)
		return 0;

	irq_set_affinity(pcfg->irq, cpumask_of(cpu));
	return 0;
}

static int bman_portal_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct bm_portal_config *pcfg;
	struct resource *addr_phys[2];
	int irq, cpu;

	pcfg = devm_kmalloc(dev, sizeof(*pcfg), GFP_KERNEL);
	if (!pcfg)
		return -ENOMEM;

	pcfg->dev = dev;

	addr_phys[0] = platform_get_resource(pdev, IORESOURCE_MEM,
					     DPAA_PORTAL_CE);
	if (!addr_phys[0]) {
		dev_err(dev, "Can't get %pOF property 'reg::CE'\n", node);
		return -ENXIO;
	}

	addr_phys[1] = platform_get_resource(pdev, IORESOURCE_MEM,
					     DPAA_PORTAL_CI);
	if (!addr_phys[1]) {
		dev_err(dev, "Can't get %pOF property 'reg::CI'\n", node);
		return -ENXIO;
	}

	pcfg->cpu = -1;

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(dev, "Can't get %pOF IRQ'\n", node);
		return -ENXIO;
	}
	pcfg->irq = irq;

	pcfg->addr_virt_ce = memremap(addr_phys[0]->start,
					resource_size(addr_phys[0]),
					QBMAN_MEMREMAP_ATTR);
	if (!pcfg->addr_virt_ce) {
		dev_err(dev, "memremap::CE failed\n");
		goto err_ioremap1;
	}

	pcfg->addr_virt_ci = ioremap(addr_phys[1]->start,
					resource_size(addr_phys[1]));
	if (!pcfg->addr_virt_ci) {
		dev_err(dev, "ioremap::CI failed\n");
		goto err_ioremap2;
	}

	spin_lock(&bman_lock);
	cpu = cpumask_next_zero(-1, &portal_cpus);
	if (cpu >= nr_cpu_ids) {
		/* unassigned portal, skip init */
		spin_unlock(&bman_lock);
		return 0;
	}

	cpumask_set_cpu(cpu, &portal_cpus);
	spin_unlock(&bman_lock);
	pcfg->cpu = cpu;

	if (!init_pcfg(pcfg)) {
		dev_err(dev, "portal init failed\n");
		goto err_portal_init;
	}

	/* clear irq affinity if assigned cpu is offline */
	if (!cpu_online(cpu))
		bman_offline_cpu(cpu);

	return 0;

err_portal_init:
	iounmap(pcfg->addr_virt_ci);
err_ioremap2:
	memunmap(pcfg->addr_virt_ce);
err_ioremap1:
	return -ENXIO;
}

static const struct of_device_id bman_portal_ids[] = {
	{
		.compatible = "fsl,bman-portal",
	},
	{}
};
MODULE_DEVICE_TABLE(of, bman_portal_ids);

static struct platform_driver bman_portal_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = bman_portal_ids,
	},
	.probe = bman_portal_probe,
};

static int __init bman_portal_driver_register(struct platform_driver *drv)
{
	int ret;

	ret = platform_driver_register(drv);
	if (ret < 0)
		return ret;

	ret = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
					"soc/qbman_portal:online",
					bman_online_cpu, bman_offline_cpu);
	if (ret < 0) {
		pr_err("bman: failed to register hotplug callbacks.\n");
		platform_driver_unregister(drv);
		return ret;
	}
	return 0;
}

module_driver(bman_portal_driver,
	      bman_portal_driver_register, platform_driver_unregister);

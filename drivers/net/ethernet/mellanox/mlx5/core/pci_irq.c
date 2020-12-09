// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/module.h>
#include <linux/mlx5/driver.h>
#include "mlx5_core.h"
#ifdef CONFIG_RFS_ACCEL
#include <linux/cpu_rmap.h>
#endif

#define MLX5_MAX_IRQ_NAME (32)

struct mlx5_irq {
	struct atomic_notifier_head nh;
	cpumask_var_t mask;
	char name[MLX5_MAX_IRQ_NAME];
};

struct mlx5_irq_table {
	struct mlx5_irq *irq;
	int nvec;
#ifdef CONFIG_RFS_ACCEL
	struct cpu_rmap *rmap;
#endif
};

int mlx5_irq_table_init(struct mlx5_core_dev *dev)
{
	struct mlx5_irq_table *irq_table;

	irq_table = kvzalloc(sizeof(*irq_table), GFP_KERNEL);
	if (!irq_table)
		return -ENOMEM;

	dev->priv.irq_table = irq_table;
	return 0;
}

void mlx5_irq_table_cleanup(struct mlx5_core_dev *dev)
{
	kvfree(dev->priv.irq_table);
}

int mlx5_irq_get_num_comp(struct mlx5_irq_table *table)
{
	return table->nvec - MLX5_IRQ_VEC_COMP_BASE;
}

static struct mlx5_irq *mlx5_irq_get(struct mlx5_core_dev *dev, int vecidx)
{
	struct mlx5_irq_table *irq_table = dev->priv.irq_table;

	return &irq_table->irq[vecidx];
}

int mlx5_irq_attach_nb(struct mlx5_irq_table *irq_table, int vecidx,
		       struct notifier_block *nb)
{
	struct mlx5_irq *irq;

	irq = &irq_table->irq[vecidx];
	return atomic_notifier_chain_register(&irq->nh, nb);
}

int mlx5_irq_detach_nb(struct mlx5_irq_table *irq_table, int vecidx,
		       struct notifier_block *nb)
{
	struct mlx5_irq *irq;

	irq = &irq_table->irq[vecidx];
	return atomic_notifier_chain_unregister(&irq->nh, nb);
}

static irqreturn_t mlx5_irq_int_handler(int irq, void *nh)
{
	atomic_notifier_call_chain(nh, 0, NULL);
	return IRQ_HANDLED;
}

static void irq_set_name(char *name, int vecidx)
{
	if (vecidx == 0) {
		snprintf(name, MLX5_MAX_IRQ_NAME, "mlx5_async");
		return;
	}

	snprintf(name, MLX5_MAX_IRQ_NAME, "mlx5_comp%d",
		 vecidx - MLX5_IRQ_VEC_COMP_BASE);
	return;
}

static int request_irqs(struct mlx5_core_dev *dev, int nvec)
{
	char name[MLX5_MAX_IRQ_NAME];
	int err;
	int i;

	for (i = 0; i < nvec; i++) {
		struct mlx5_irq *irq = mlx5_irq_get(dev, i);
		int irqn = pci_irq_vector(dev->pdev, i);

		irq_set_name(name, i);
		ATOMIC_INIT_NOTIFIER_HEAD(&irq->nh);
		snprintf(irq->name, MLX5_MAX_IRQ_NAME,
			 "%s@pci:%s", name, pci_name(dev->pdev));
		err = request_irq(irqn, mlx5_irq_int_handler, 0, irq->name,
				  &irq->nh);
		if (err) {
			mlx5_core_err(dev, "Failed to request irq\n");
			goto err_request_irq;
		}
	}
	return 0;

err_request_irq:
	while (i--) {
		struct mlx5_irq *irq = mlx5_irq_get(dev, i);
		int irqn = pci_irq_vector(dev->pdev, i);

		free_irq(irqn, &irq->nh);
	}
	return  err;
}

static void irq_clear_rmap(struct mlx5_core_dev *dev)
{
#ifdef CONFIG_RFS_ACCEL
	struct mlx5_irq_table *irq_table = dev->priv.irq_table;

	free_irq_cpu_rmap(irq_table->rmap);
#endif
}

static int irq_set_rmap(struct mlx5_core_dev *mdev)
{
	int err = 0;
#ifdef CONFIG_RFS_ACCEL
	struct mlx5_irq_table *irq_table = mdev->priv.irq_table;
	int num_affinity_vec;
	int vecidx;

	num_affinity_vec = mlx5_irq_get_num_comp(irq_table);
	irq_table->rmap = alloc_irq_cpu_rmap(num_affinity_vec);
	if (!irq_table->rmap) {
		err = -ENOMEM;
		mlx5_core_err(mdev, "Failed to allocate cpu_rmap. err %d", err);
		goto err_out;
	}

	vecidx = MLX5_IRQ_VEC_COMP_BASE;
	for (; vecidx < irq_table->nvec; vecidx++) {
		err = irq_cpu_rmap_add(irq_table->rmap,
				       pci_irq_vector(mdev->pdev, vecidx));
		if (err) {
			mlx5_core_err(mdev, "irq_cpu_rmap_add failed. err %d",
				      err);
			goto err_irq_cpu_rmap_add;
		}
	}
	return 0;

err_irq_cpu_rmap_add:
	irq_clear_rmap(mdev);
err_out:
#endif
	return err;
}

/* Completion IRQ vectors */

static int set_comp_irq_affinity_hint(struct mlx5_core_dev *mdev, int i)
{
	int vecidx = MLX5_IRQ_VEC_COMP_BASE + i;
	struct mlx5_irq *irq;
	int irqn;

	irq = mlx5_irq_get(mdev, vecidx);
	irqn = pci_irq_vector(mdev->pdev, vecidx);
	if (!zalloc_cpumask_var(&irq->mask, GFP_KERNEL)) {
		mlx5_core_warn(mdev, "zalloc_cpumask_var failed");
		return -ENOMEM;
	}

	cpumask_set_cpu(cpumask_local_spread(i, mdev->priv.numa_node),
			irq->mask);
	if (IS_ENABLED(CONFIG_SMP) &&
	    irq_set_affinity_hint(irqn, irq->mask))
		mlx5_core_warn(mdev, "irq_set_affinity_hint failed, irq 0x%.4x",
			       irqn);

	return 0;
}

static void clear_comp_irq_affinity_hint(struct mlx5_core_dev *mdev, int i)
{
	int vecidx = MLX5_IRQ_VEC_COMP_BASE + i;
	struct mlx5_irq *irq;
	int irqn;

	irq = mlx5_irq_get(mdev, vecidx);
	irqn = pci_irq_vector(mdev->pdev, vecidx);
	irq_set_affinity_hint(irqn, NULL);
	free_cpumask_var(irq->mask);
}

static int set_comp_irq_affinity_hints(struct mlx5_core_dev *mdev)
{
	int nvec = mlx5_irq_get_num_comp(mdev->priv.irq_table);
	int err;
	int i;

	for (i = 0; i < nvec; i++) {
		err = set_comp_irq_affinity_hint(mdev, i);
		if (err)
			goto err_out;
	}

	return 0;

err_out:
	for (i--; i >= 0; i--)
		clear_comp_irq_affinity_hint(mdev, i);

	return err;
}

static void clear_comp_irqs_affinity_hints(struct mlx5_core_dev *mdev)
{
	int nvec = mlx5_irq_get_num_comp(mdev->priv.irq_table);
	int i;

	for (i = 0; i < nvec; i++)
		clear_comp_irq_affinity_hint(mdev, i);
}

struct cpumask *
mlx5_irq_get_affinity_mask(struct mlx5_irq_table *irq_table, int vecidx)
{
	return irq_table->irq[vecidx].mask;
}

#ifdef CONFIG_RFS_ACCEL
struct cpu_rmap *mlx5_irq_get_rmap(struct mlx5_irq_table *irq_table)
{
	return irq_table->rmap;
}
#endif

static void unrequest_irqs(struct mlx5_core_dev *dev)
{
	struct mlx5_irq_table *table = dev->priv.irq_table;
	int i;

	for (i = 0; i < table->nvec; i++)
		free_irq(pci_irq_vector(dev->pdev, i),
			 &mlx5_irq_get(dev, i)->nh);
}

int mlx5_irq_table_create(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;
	struct mlx5_irq_table *table = priv->irq_table;
	int num_eqs = MLX5_CAP_GEN(dev, max_num_eqs) ?
		      MLX5_CAP_GEN(dev, max_num_eqs) :
		      1 << MLX5_CAP_GEN(dev, log_max_eq);
	int nvec;
	int err;

	nvec = MLX5_CAP_GEN(dev, num_ports) * num_online_cpus() +
	       MLX5_IRQ_VEC_COMP_BASE;
	nvec = min_t(int, nvec, num_eqs);
	if (nvec <= MLX5_IRQ_VEC_COMP_BASE)
		return -ENOMEM;

	table->irq = kcalloc(nvec, sizeof(*table->irq), GFP_KERNEL);
	if (!table->irq)
		return -ENOMEM;

	nvec = pci_alloc_irq_vectors(dev->pdev, MLX5_IRQ_VEC_COMP_BASE + 1,
				     nvec, PCI_IRQ_MSIX);
	if (nvec < 0) {
		err = nvec;
		goto err_free_irq;
	}

	table->nvec = nvec;

	err = irq_set_rmap(dev);
	if (err)
		goto err_set_rmap;

	err = request_irqs(dev, nvec);
	if (err)
		goto err_request_irqs;

	err = set_comp_irq_affinity_hints(dev);
	if (err) {
		mlx5_core_err(dev, "Failed to alloc affinity hint cpumask\n");
		goto err_set_affinity;
	}

	return 0;

err_set_affinity:
	unrequest_irqs(dev);
err_request_irqs:
	irq_clear_rmap(dev);
err_set_rmap:
	pci_free_irq_vectors(dev->pdev);
err_free_irq:
	kfree(table->irq);
	return err;
}

void mlx5_irq_table_destroy(struct mlx5_core_dev *dev)
{
	struct mlx5_irq_table *table = dev->priv.irq_table;
	int i;

	/* free_irq requires that affinity and rmap will be cleared
	 * before calling it. This is why there is asymmetry with set_rmap
	 * which should be called after alloc_irq but before request_irq.
	 */
	irq_clear_rmap(dev);
	clear_comp_irqs_affinity_hints(dev);
	for (i = 0; i < table->nvec; i++)
		free_irq(pci_irq_vector(dev->pdev, i),
			 &mlx5_irq_get(dev, i)->nh);
	pci_free_irq_vectors(dev->pdev);
	kfree(table->irq);
}


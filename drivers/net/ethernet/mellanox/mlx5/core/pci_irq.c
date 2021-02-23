// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/module.h>
#include <linux/mlx5/driver.h>
#include "mlx5_core.h"
#include "mlx5_irq.h"
#ifdef CONFIG_RFS_ACCEL
#include <linux/cpu_rmap.h>
#endif

#define MLX5_MAX_IRQ_NAME (32)

struct mlx5_irq {
	u32 index;
	struct atomic_notifier_head nh;
	cpumask_var_t mask;
	char name[MLX5_MAX_IRQ_NAME];
	struct kref kref;
	int irqn;
	struct mlx5_irq_table *table;
};

struct mlx5_irq_table {
	struct xarray irqs;
	int nvec;
};

int mlx5_irq_table_init(struct mlx5_core_dev *dev)
{
	struct mlx5_irq_table *irq_table;

	if (mlx5_core_is_sf(dev))
		return 0;

	irq_table = kvzalloc(sizeof(*irq_table), GFP_KERNEL);
	if (!irq_table)
		return -ENOMEM;

	dev->priv.irq_table = irq_table;
	return 0;
}

void mlx5_irq_table_cleanup(struct mlx5_core_dev *dev)
{
	if (mlx5_core_is_sf(dev))
		return;

	kvfree(dev->priv.irq_table);
}

int mlx5_irq_get_num_comp(struct mlx5_irq_table *table)
{
	return table->nvec - MLX5_IRQ_VEC_COMP_BASE;
}

/**
 * mlx5_get_default_msix_vec_count - Get the default number of MSI-X vectors
 *                                   to be ssigned to each VF.
 * @dev: PF to work on
 * @num_vfs: Number of enabled VFs
 */
int mlx5_get_default_msix_vec_count(struct mlx5_core_dev *dev, int num_vfs)
{
	int num_vf_msix, min_msix, max_msix;

	num_vf_msix = MLX5_CAP_GEN_MAX(dev, num_total_dynamic_vf_msix);
	if (!num_vf_msix)
		return 0;

	min_msix = MLX5_CAP_GEN(dev, min_dynamic_vf_msix_table_size);
	max_msix = MLX5_CAP_GEN(dev, max_dynamic_vf_msix_table_size);

	/* Limit maximum number of MSI-X vectors so the default configuration
	 * has some available in the pool. This will allow the user to increase
	 * the number of vectors in a VF without having to first size-down other
	 * VFs.
	 */
	return max(min(num_vf_msix / num_vfs, max_msix / 2), min_msix);
}

/**
 * mlx5_set_msix_vec_count - Set dynamically allocated MSI-X on the VF
 * @dev: PF to work on
 * @function_id: Internal PCI VF function IDd
 * @msix_vec_count: Number of MSI-X vectors to set
 */
int mlx5_set_msix_vec_count(struct mlx5_core_dev *dev, int function_id,
			    int msix_vec_count)
{
	int query_sz = MLX5_ST_SZ_BYTES(query_hca_cap_out);
	int set_sz = MLX5_ST_SZ_BYTES(set_hca_cap_in);
	void *hca_cap = NULL, *query_cap = NULL, *cap;
	int num_vf_msix, min_msix, max_msix;
	int ret;

	num_vf_msix = MLX5_CAP_GEN_MAX(dev, num_total_dynamic_vf_msix);
	if (!num_vf_msix)
		return 0;

	if (!MLX5_CAP_GEN(dev, vport_group_manager) || !mlx5_core_is_pf(dev))
		return -EOPNOTSUPP;

	min_msix = MLX5_CAP_GEN(dev, min_dynamic_vf_msix_table_size);
	max_msix = MLX5_CAP_GEN(dev, max_dynamic_vf_msix_table_size);

	if (msix_vec_count < min_msix)
		return -EINVAL;

	if (msix_vec_count > max_msix)
		return -EOVERFLOW;

	query_cap = kzalloc(query_sz, GFP_KERNEL);
	hca_cap = kzalloc(set_sz, GFP_KERNEL);
	if (!hca_cap || !query_cap) {
		ret = -ENOMEM;
		goto out;
	}

	ret = mlx5_vport_get_other_func_cap(dev, function_id, query_cap);
	if (ret)
		goto out;

	cap = MLX5_ADDR_OF(set_hca_cap_in, hca_cap, capability);
	memcpy(cap, MLX5_ADDR_OF(query_hca_cap_out, query_cap, capability),
	       MLX5_UN_SZ_BYTES(hca_cap_union));
	MLX5_SET(cmd_hca_cap, cap, dynamic_msix_table_size, msix_vec_count);

	MLX5_SET(set_hca_cap_in, hca_cap, opcode, MLX5_CMD_OP_SET_HCA_CAP);
	MLX5_SET(set_hca_cap_in, hca_cap, other_function, 1);
	MLX5_SET(set_hca_cap_in, hca_cap, function_id, function_id);

	MLX5_SET(set_hca_cap_in, hca_cap, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE << 1);
	ret = mlx5_cmd_exec_in(dev, set_hca_cap, hca_cap);
out:
	kfree(hca_cap);
	kfree(query_cap);
	return ret;
}

static void irq_release(struct kref *kref)
{
	struct mlx5_irq *irq = container_of(kref, struct mlx5_irq, kref);
	struct mlx5_irq_table *table =  irq->table;

	xa_erase(&table->irqs, irq->index);
	/* free_irq requires that affinity and rmap will be cleared
	 * before calling it. This is why there is asymmetry with set_rmap
	 * which should be called after alloc_irq but before request_irq.
	 */
	irq_set_affinity_hint(irq->irqn, NULL);
	free_cpumask_var(irq->mask);
	free_irq(irq->irqn, &irq->nh);
	kfree(irq);
}

static void irq_put(struct mlx5_irq *irq)
{
	kref_put(&irq->kref, irq_release);
}

int mlx5_irq_attach_nb(struct mlx5_irq *irq, struct notifier_block *nb)
{
	int err;

	err = kref_get_unless_zero(&irq->kref);
	if (WARN_ON_ONCE(!err))
		/* Something very bad happens here, we are enabling EQ
		 * on non-existing IRQ.
		 */
		return -ENOENT;
	err = atomic_notifier_chain_register(&irq->nh, nb);
	if (err)
		irq_put(irq);
	return err;
}

int mlx5_irq_detach_nb(struct mlx5_irq *irq, struct notifier_block *nb)
{
	irq_put(irq);
	return atomic_notifier_chain_unregister(&irq->nh, nb);
}

static irqreturn_t irq_int_handler(int irq, void *nh)
{
	atomic_notifier_call_chain(nh, 0, NULL);
	return IRQ_HANDLED;
}

static void irq_set_name(char *name, int vecidx)
{
	if (!vecidx) {
		snprintf(name, MLX5_MAX_IRQ_NAME, "mlx5_async");
		return;
	}

	snprintf(name, MLX5_MAX_IRQ_NAME, "mlx5_comp%d",
		 vecidx - MLX5_IRQ_VEC_COMP_BASE);
}

static struct mlx5_irq *irq_request(struct mlx5_core_dev *dev, int i)
{
	struct mlx5_irq_table *table = mlx5_irq_table_get(dev);
	char name[MLX5_MAX_IRQ_NAME];
	struct xa_limit xa_num_irqs;
	struct mlx5_irq *irq;
	int err;

	irq = kzalloc(sizeof(*irq), GFP_KERNEL);
	if (!irq)
		return ERR_PTR(-ENOMEM);
	irq->irqn = pci_irq_vector(dev->pdev, i);
	irq_set_name(name, i);
	ATOMIC_INIT_NOTIFIER_HEAD(&irq->nh);
	snprintf(irq->name, MLX5_MAX_IRQ_NAME,
		 "%s@pci:%s", name, pci_name(dev->pdev));
	err = request_irq(irq->irqn, irq_int_handler, 0, irq->name,
			  &irq->nh);
	if (err) {
		mlx5_core_err(dev, "Failed to request irq. err = %d\n", err);
		goto err_req_irq;
	}
	if (!zalloc_cpumask_var(&irq->mask, GFP_KERNEL)) {
		mlx5_core_warn(dev, "zalloc_cpumask_var failed\n");
		err = -ENOMEM;
		goto err_cpumask;
	}
	xa_num_irqs.min = 0;
	xa_num_irqs.max = table->nvec;
	err = xa_alloc(&table->irqs, &irq->index, irq, xa_num_irqs,
		       GFP_KERNEL);
	if (err) {
		mlx5_core_err(dev, "Failed to alloc xa entry for irq(%u). err = %d\n",
			      irq->index, err);
		goto err_xa;
	}
	irq->table = table;
	kref_init(&irq->kref);
	return irq;
err_xa:
	free_cpumask_var(irq->mask);
err_cpumask:
	free_irq(irq->irqn, &irq->nh);
err_req_irq:
	kfree(irq);
	return ERR_PTR(err);
}

/**
 * mlx5_irq_release - release an IRQ back to the system.
 * @irq: irq to be released.
 */
void mlx5_irq_release(struct mlx5_irq *irq)
{
	synchronize_irq(irq->irqn);
	irq_put(irq);
}

/**
 * mlx5_irq_request - request an IRQ for mlx5 device.
 * @dev: mlx5 device that requesting the IRQ.
 * @vecidx: vector index of the IRQ. This argument is ignore if affinity is
 * provided.
 * @affinity: cpumask requested for this IRQ.
 *
 * This function returns a pointer to IRQ, or ERR_PTR in case of error.
 */
struct mlx5_irq *mlx5_irq_request(struct mlx5_core_dev *dev, int vecidx,
				  struct cpumask *affinity)
{
	struct mlx5_irq_table *irq_table = mlx5_irq_table_get(dev);
	struct mlx5_irq *irq;

	irq = xa_load(&irq_table->irqs, vecidx);
	if (irq) {
		kref_get(&irq->kref);
		return irq;
	}
	irq = irq_request(dev, vecidx);
	if (IS_ERR(irq))
		return irq;
	cpumask_copy(irq->mask, affinity);
	irq_set_affinity_hint(irq->irqn, irq->mask);
	return irq;
}

struct cpumask *mlx5_irq_get_affinity_mask(struct mlx5_irq *irq)
{
	return irq->mask;
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

	if (mlx5_core_is_sf(dev))
		return 0;

	nvec = MLX5_CAP_GEN(dev, num_ports) * num_online_cpus() +
	       MLX5_IRQ_VEC_COMP_BASE;
	nvec = min_t(int, nvec, num_eqs);
	if (nvec <= MLX5_IRQ_VEC_COMP_BASE)
		return -ENOMEM;

	xa_init_flags(&table->irqs, XA_FLAGS_ALLOC);

	nvec = pci_alloc_irq_vectors(dev->pdev, MLX5_IRQ_VEC_COMP_BASE + 1,
				     nvec, PCI_IRQ_MSIX);
	if (nvec < 0) {
		err = nvec;
		goto err_free_irq;
	}

	table->nvec = nvec;

	return 0;

err_free_irq:
	xa_destroy(&table->irqs);
	return err;
}

void mlx5_irq_table_destroy(struct mlx5_core_dev *dev)
{
	struct mlx5_irq_table *table = dev->priv.irq_table;
	struct mlx5_irq *irq;
	unsigned long index;

	if (mlx5_core_is_sf(dev))
		return;

	/* There are cases where IRQs still will be in used when we reaching
	 * to here. Hence, making sure all the irqs are realeased.
	 */
	xa_for_each(&table->irqs, index, irq)
		irq_release(&irq->kref);
	pci_free_irq_vectors(dev->pdev);
	xa_destroy(&table->irqs);
}

struct mlx5_irq_table *mlx5_irq_table_get(struct mlx5_core_dev *dev)
{
#ifdef CONFIG_MLX5_SF
	if (mlx5_core_is_sf(dev))
		return dev->priv.parent_mdev->priv.irq_table;
#endif
	return dev->priv.irq_table;
}

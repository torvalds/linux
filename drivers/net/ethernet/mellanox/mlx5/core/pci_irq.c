// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/vport.h>
#include "mlx5_core.h"
#include "mlx5_irq.h"
#include "pci_irq.h"
#include "lib/sf.h"
#include "lib/eq.h"
#ifdef CONFIG_RFS_ACCEL
#include <linux/cpu_rmap.h>
#endif

#define MLX5_SFS_PER_CTRL_IRQ 64
#define MLX5_IRQ_CTRL_SF_MAX 8
/* min num of vectors for SFs to be enabled */
#define MLX5_IRQ_VEC_COMP_BASE_SF 2
#define MLX5_IRQ_VEC_COMP_BASE 1

#define MLX5_EQ_SHARE_IRQ_MAX_COMP (8)
#define MLX5_EQ_SHARE_IRQ_MAX_CTRL (UINT_MAX)
#define MLX5_EQ_SHARE_IRQ_MIN_COMP (1)
#define MLX5_EQ_SHARE_IRQ_MIN_CTRL (4)

struct mlx5_irq {
	struct atomic_notifier_head nh;
	cpumask_var_t mask;
	char name[MLX5_MAX_IRQ_FORMATTED_NAME];
	struct mlx5_irq_pool *pool;
	int refcount;
	struct msi_map map;
	u32 pool_index;
};

struct mlx5_irq_table {
	struct mlx5_irq_pool *pcif_pool;
	struct mlx5_irq_pool *sf_ctrl_pool;
	struct mlx5_irq_pool *sf_comp_pool;
};

static int mlx5_core_func_to_vport(const struct mlx5_core_dev *dev,
				   int func,
				   bool ec_vf_func)
{
	if (!ec_vf_func)
		return func;
	return mlx5_core_ec_vf_vport_base(dev) + func - 1;
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
	bool ec_vf_function;
	int vport;
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

	query_cap = kvzalloc(query_sz, GFP_KERNEL);
	hca_cap = kvzalloc(set_sz, GFP_KERNEL);
	if (!hca_cap || !query_cap) {
		ret = -ENOMEM;
		goto out;
	}

	ec_vf_function = mlx5_core_ec_sriov_enabled(dev);
	vport = mlx5_core_func_to_vport(dev, function_id, ec_vf_function);
	ret = mlx5_vport_get_other_func_general_cap(dev, vport, query_cap);
	if (ret)
		goto out;

	cap = MLX5_ADDR_OF(set_hca_cap_in, hca_cap, capability);
	memcpy(cap, MLX5_ADDR_OF(query_hca_cap_out, query_cap, capability),
	       MLX5_UN_SZ_BYTES(hca_cap_union));
	MLX5_SET(cmd_hca_cap, cap, dynamic_msix_table_size, msix_vec_count);

	MLX5_SET(set_hca_cap_in, hca_cap, opcode, MLX5_CMD_OP_SET_HCA_CAP);
	MLX5_SET(set_hca_cap_in, hca_cap, other_function, 1);
	MLX5_SET(set_hca_cap_in, hca_cap, ec_vf_function, ec_vf_function);
	MLX5_SET(set_hca_cap_in, hca_cap, function_id, function_id);

	MLX5_SET(set_hca_cap_in, hca_cap, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE << 1);
	ret = mlx5_cmd_exec_in(dev, set_hca_cap, hca_cap);
out:
	kvfree(hca_cap);
	kvfree(query_cap);
	return ret;
}

/* mlx5_system_free_irq - Free an IRQ
 * @irq: IRQ to free
 *
 * Free the IRQ and other resources such as rmap from the system.
 * BUT doesn't free or remove reference from mlx5.
 * This function is very important for the shutdown flow, where we need to
 * cleanup system resoruces but keep mlx5 objects alive,
 * see mlx5_irq_table_free_irqs().
 */
static void mlx5_system_free_irq(struct mlx5_irq *irq)
{
	struct mlx5_irq_pool *pool = irq->pool;
#ifdef CONFIG_RFS_ACCEL
	struct cpu_rmap *rmap;
#endif

	/* free_irq requires that affinity_hint and rmap will be cleared before
	 * calling it. To satisfy this requirement, we call
	 * irq_cpu_rmap_remove() to remove the notifier
	 */
	irq_update_affinity_hint(irq->map.virq, NULL);
#ifdef CONFIG_RFS_ACCEL
	rmap = mlx5_eq_table_get_rmap(pool->dev);
	if (rmap)
		irq_cpu_rmap_remove(rmap, irq->map.virq);
#endif

	free_irq(irq->map.virq, &irq->nh);
	if (irq->map.index && pci_msix_can_alloc_dyn(pool->dev->pdev))
		pci_msix_free_irq(pool->dev->pdev, irq->map);
}

static void irq_release(struct mlx5_irq *irq)
{
	struct mlx5_irq_pool *pool = irq->pool;

	xa_erase(&pool->irqs, irq->pool_index);
	mlx5_system_free_irq(irq);
	free_cpumask_var(irq->mask);
	kfree(irq);
}

int mlx5_irq_put(struct mlx5_irq *irq)
{
	struct mlx5_irq_pool *pool = irq->pool;
	int ret = 0;

	mutex_lock(&pool->lock);
	irq->refcount--;
	if (!irq->refcount) {
		irq_release(irq);
		ret = 1;
	}
	mutex_unlock(&pool->lock);
	return ret;
}

int mlx5_irq_read_locked(struct mlx5_irq *irq)
{
	lockdep_assert_held(&irq->pool->lock);
	return irq->refcount;
}

int mlx5_irq_get_locked(struct mlx5_irq *irq)
{
	lockdep_assert_held(&irq->pool->lock);
	if (WARN_ON_ONCE(!irq->refcount))
		return 0;
	irq->refcount++;
	return 1;
}

static int irq_get(struct mlx5_irq *irq)
{
	int err;

	mutex_lock(&irq->pool->lock);
	err = mlx5_irq_get_locked(irq);
	mutex_unlock(&irq->pool->lock);
	return err;
}

static irqreturn_t irq_int_handler(int irq, void *nh)
{
	atomic_notifier_call_chain(nh, 0, NULL);
	return IRQ_HANDLED;
}

static void irq_sf_set_name(struct mlx5_irq_pool *pool, char *name, int vecidx)
{
	snprintf(name, MLX5_MAX_IRQ_NAME, "%s%d", pool->name, vecidx);
}

static void irq_set_name(struct mlx5_irq_pool *pool, char *name, int vecidx)
{
	if (!pool->xa_num_irqs.max) {
		/* in case we only have a single irq for the device */
		snprintf(name, MLX5_MAX_IRQ_NAME, "mlx5_combined%d", vecidx);
		return;
	}

	if (!vecidx) {
		snprintf(name, MLX5_MAX_IRQ_NAME, "mlx5_async%d", vecidx);
		return;
	}

	vecidx -= MLX5_IRQ_VEC_COMP_BASE;
	snprintf(name, MLX5_MAX_IRQ_NAME, "mlx5_comp%d", vecidx);
}

struct mlx5_irq *mlx5_irq_alloc(struct mlx5_irq_pool *pool, int i,
				struct irq_affinity_desc *af_desc,
				struct cpu_rmap **rmap)
{
	struct mlx5_core_dev *dev = pool->dev;
	char name[MLX5_MAX_IRQ_NAME];
	struct mlx5_irq *irq;
	int err;

	irq = kzalloc(sizeof(*irq), GFP_KERNEL);
	if (!irq || !zalloc_cpumask_var(&irq->mask, GFP_KERNEL)) {
		kfree(irq);
		return ERR_PTR(-ENOMEM);
	}

	if (!i || !pci_msix_can_alloc_dyn(dev->pdev)) {
		/* The vector at index 0 is always statically allocated. If
		 * dynamic irq is not supported all vectors are statically
		 * allocated. In both cases just get the irq number and set
		 * the index.
		 */
		irq->map.virq = pci_irq_vector(dev->pdev, i);
		irq->map.index = i;
	} else {
		irq->map = pci_msix_alloc_irq_at(dev->pdev, MSI_ANY_INDEX, af_desc);
		if (!irq->map.virq) {
			err = irq->map.index;
			goto err_alloc_irq;
		}
	}

	if (i && rmap && *rmap) {
#ifdef CONFIG_RFS_ACCEL
		err = irq_cpu_rmap_add(*rmap, irq->map.virq);
		if (err)
			goto err_irq_rmap;
#endif
	}
	if (!mlx5_irq_pool_is_sf_pool(pool))
		irq_set_name(pool, name, i);
	else
		irq_sf_set_name(pool, name, i);
	ATOMIC_INIT_NOTIFIER_HEAD(&irq->nh);
	snprintf(irq->name, MLX5_MAX_IRQ_FORMATTED_NAME,
		 MLX5_IRQ_NAME_FORMAT_STR, name, pci_name(dev->pdev));
	err = request_irq(irq->map.virq, irq_int_handler, 0, irq->name,
			  &irq->nh);
	if (err) {
		mlx5_core_err(dev, "Failed to request irq. err = %d\n", err);
		goto err_req_irq;
	}

	if (af_desc) {
		cpumask_copy(irq->mask, &af_desc->mask);
		irq_set_affinity_and_hint(irq->map.virq, irq->mask);
	}
	irq->pool = pool;
	irq->refcount = 1;
	irq->pool_index = i;
	err = xa_err(xa_store(&pool->irqs, irq->pool_index, irq, GFP_KERNEL));
	if (err) {
		mlx5_core_err(dev, "Failed to alloc xa entry for irq(%u). err = %d\n",
			      irq->pool_index, err);
		goto err_xa;
	}
	return irq;
err_xa:
	if (af_desc)
		irq_update_affinity_hint(irq->map.virq, NULL);
	free_irq(irq->map.virq, &irq->nh);
err_req_irq:
#ifdef CONFIG_RFS_ACCEL
	if (i && rmap && *rmap) {
		free_irq_cpu_rmap(*rmap);
		*rmap = NULL;
	}
err_irq_rmap:
#endif
	if (i && pci_msix_can_alloc_dyn(dev->pdev))
		pci_msix_free_irq(dev->pdev, irq->map);
err_alloc_irq:
	free_cpumask_var(irq->mask);
	kfree(irq);
	return ERR_PTR(err);
}

int mlx5_irq_attach_nb(struct mlx5_irq *irq, struct notifier_block *nb)
{
	int ret;

	ret = irq_get(irq);
	if (!ret)
		/* Something very bad happens here, we are enabling EQ
		 * on non-existing IRQ.
		 */
		return -ENOENT;
	ret = atomic_notifier_chain_register(&irq->nh, nb);
	if (ret)
		mlx5_irq_put(irq);
	return ret;
}

int mlx5_irq_detach_nb(struct mlx5_irq *irq, struct notifier_block *nb)
{
	int err = 0;

	err = atomic_notifier_chain_unregister(&irq->nh, nb);
	mlx5_irq_put(irq);
	return err;
}

struct cpumask *mlx5_irq_get_affinity_mask(struct mlx5_irq *irq)
{
	return irq->mask;
}

int mlx5_irq_get_index(struct mlx5_irq *irq)
{
	return irq->map.index;
}

/* irq_pool API */

/* requesting an irq from a given pool according to given index */
static struct mlx5_irq *
irq_pool_request_vector(struct mlx5_irq_pool *pool, int vecidx,
			struct irq_affinity_desc *af_desc,
			struct cpu_rmap **rmap)
{
	struct mlx5_irq *irq;

	mutex_lock(&pool->lock);
	irq = xa_load(&pool->irqs, vecidx);
	if (irq) {
		mlx5_irq_get_locked(irq);
		goto unlock;
	}
	irq = mlx5_irq_alloc(pool, vecidx, af_desc, rmap);
unlock:
	mutex_unlock(&pool->lock);
	return irq;
}

static struct mlx5_irq_pool *sf_ctrl_irq_pool_get(struct mlx5_irq_table *irq_table)
{
	return irq_table->sf_ctrl_pool;
}

static struct mlx5_irq_pool *sf_irq_pool_get(struct mlx5_irq_table *irq_table)
{
	return irq_table->sf_comp_pool;
}

struct mlx5_irq_pool *mlx5_irq_pool_get(struct mlx5_core_dev *dev)
{
	struct mlx5_irq_table *irq_table = mlx5_irq_table_get(dev);
	struct mlx5_irq_pool *pool = NULL;

	if (mlx5_core_is_sf(dev))
		pool = sf_irq_pool_get(irq_table);

	/* In some configs, there won't be a pool of SFs IRQs. Hence, returning
	 * the PF IRQs pool in case the SF pool doesn't exist.
	 */
	return pool ? pool : irq_table->pcif_pool;
}

static struct mlx5_irq_pool *ctrl_irq_pool_get(struct mlx5_core_dev *dev)
{
	struct mlx5_irq_table *irq_table = mlx5_irq_table_get(dev);
	struct mlx5_irq_pool *pool = NULL;

	if (mlx5_core_is_sf(dev))
		pool = sf_ctrl_irq_pool_get(irq_table);

	/* In some configs, there won't be a pool of SFs IRQs. Hence, returning
	 * the PF IRQs pool in case the SF pool doesn't exist.
	 */
	return pool ? pool : irq_table->pcif_pool;
}

static void _mlx5_irq_release(struct mlx5_irq *irq)
{
	synchronize_irq(irq->map.virq);
	mlx5_irq_put(irq);
}

/**
 * mlx5_ctrl_irq_release - release a ctrl IRQ back to the system.
 * @ctrl_irq: ctrl IRQ to be released.
 */
void mlx5_ctrl_irq_release(struct mlx5_irq *ctrl_irq)
{
	_mlx5_irq_release(ctrl_irq);
}

/**
 * mlx5_ctrl_irq_request - request a ctrl IRQ for mlx5 device.
 * @dev: mlx5 device that requesting the IRQ.
 *
 * This function returns a pointer to IRQ, or ERR_PTR in case of error.
 */
struct mlx5_irq *mlx5_ctrl_irq_request(struct mlx5_core_dev *dev)
{
	struct mlx5_irq_pool *pool = ctrl_irq_pool_get(dev);
	struct irq_affinity_desc af_desc;
	struct mlx5_irq *irq;

	cpumask_copy(&af_desc.mask, cpu_online_mask);
	af_desc.is_managed = false;
	if (!mlx5_irq_pool_is_sf_pool(pool)) {
		/* In case we are allocating a control IRQ from a pci device's pool.
		 * This can happen also for a SF if the SFs pool is empty.
		 */
		if (!pool->xa_num_irqs.max) {
			cpumask_clear(&af_desc.mask);
			/* In case we only have a single IRQ for PF/VF */
			cpumask_set_cpu(cpumask_first(cpu_online_mask), &af_desc.mask);
		}
		/* Allocate the IRQ in index 0. The vector was already allocated */
		irq = irq_pool_request_vector(pool, 0, &af_desc, NULL);
	} else {
		irq = mlx5_irq_affinity_request(pool, &af_desc);
	}

	return irq;
}

/**
 * mlx5_irq_request - request an IRQ for mlx5 PF/VF device.
 * @dev: mlx5 device that requesting the IRQ.
 * @vecidx: vector index of the IRQ. This argument is ignore if affinity is
 * provided.
 * @af_desc: affinity descriptor for this IRQ.
 * @rmap: pointer to reverse map pointer for completion interrupts
 *
 * This function returns a pointer to IRQ, or ERR_PTR in case of error.
 */
struct mlx5_irq *mlx5_irq_request(struct mlx5_core_dev *dev, u16 vecidx,
				  struct irq_affinity_desc *af_desc,
				  struct cpu_rmap **rmap)
{
	struct mlx5_irq_table *irq_table = mlx5_irq_table_get(dev);
	struct mlx5_irq_pool *pool;
	struct mlx5_irq *irq;

	pool = irq_table->pcif_pool;
	irq = irq_pool_request_vector(pool, vecidx, af_desc, rmap);
	if (IS_ERR(irq))
		return irq;
	mlx5_core_dbg(dev, "irq %u mapped to cpu %*pbl, %u EQs on this irq\n",
		      irq->map.virq, cpumask_pr_args(&af_desc->mask),
		      irq->refcount / MLX5_EQ_REFS_PER_IRQ);
	return irq;
}

/**
 * mlx5_msix_alloc - allocate msix interrupt
 * @dev: mlx5 device from which to request
 * @handler: interrupt handler
 * @affdesc: affinity descriptor
 * @name: interrupt name
 *
 * Returns: struct msi_map with result encoded.
 * Note: the caller must make sure to release the irq by calling
 *       mlx5_msix_free() if shutdown was initiated.
 */
struct msi_map mlx5_msix_alloc(struct mlx5_core_dev *dev,
			       irqreturn_t (*handler)(int, void *),
			       const struct irq_affinity_desc *affdesc,
			       const char *name)
{
	struct msi_map map;
	int err;

	if (!dev->pdev) {
		map.virq = 0;
		map.index = -EINVAL;
		return map;
	}

	map = pci_msix_alloc_irq_at(dev->pdev, MSI_ANY_INDEX, affdesc);
	if (!map.virq)
		return map;

	err = request_irq(map.virq, handler, 0, name, NULL);
	if (err) {
		mlx5_core_warn(dev, "err %d\n", err);
		pci_msix_free_irq(dev->pdev, map);
		map.virq = 0;
		map.index = -ENOMEM;
	}
	return map;
}
EXPORT_SYMBOL(mlx5_msix_alloc);

/**
 * mlx5_msix_free - free a previously allocated msix interrupt
 * @dev: mlx5 device associated with interrupt
 * @map: map previously returned by mlx5_msix_alloc()
 */
void mlx5_msix_free(struct mlx5_core_dev *dev, struct msi_map map)
{
	free_irq(map.virq, NULL);
	pci_msix_free_irq(dev->pdev, map);
}
EXPORT_SYMBOL(mlx5_msix_free);

/**
 * mlx5_irq_release_vector - release one IRQ back to the system.
 * @irq: the irq to release.
 */
void mlx5_irq_release_vector(struct mlx5_irq *irq)
{
	_mlx5_irq_release(irq);
}

/**
 * mlx5_irq_request_vector - request one IRQ for mlx5 device.
 * @dev: mlx5 device that is requesting the IRQ.
 * @cpu: CPU to bind the IRQ to.
 * @vecidx: vector index to request an IRQ for.
 * @rmap: pointer to reverse map pointer for completion interrupts
 *
 * Each IRQ is bound to at most 1 CPU.
 * This function is requests one IRQ, for the given @vecidx.
 *
 * This function returns a pointer to the irq on success, or an error pointer
 * in case of an error.
 */
struct mlx5_irq *mlx5_irq_request_vector(struct mlx5_core_dev *dev, u16 cpu,
					 u16 vecidx, struct cpu_rmap **rmap)
{
	struct mlx5_irq_table *table = mlx5_irq_table_get(dev);
	struct mlx5_irq_pool *pool = table->pcif_pool;
	struct irq_affinity_desc af_desc;
	int offset = MLX5_IRQ_VEC_COMP_BASE;

	if (!pool->xa_num_irqs.max)
		offset = 0;

	af_desc.is_managed = false;
	cpumask_clear(&af_desc.mask);
	cpumask_set_cpu(cpu, &af_desc.mask);
	return mlx5_irq_request(dev, vecidx + offset, &af_desc, rmap);
}

static struct mlx5_irq_pool *
irq_pool_alloc(struct mlx5_core_dev *dev, int start, int size, char *name,
	       u32 min_threshold, u32 max_threshold)
{
	struct mlx5_irq_pool *pool = kvzalloc(sizeof(*pool), GFP_KERNEL);

	if (!pool)
		return ERR_PTR(-ENOMEM);
	pool->dev = dev;
	mutex_init(&pool->lock);
	xa_init_flags(&pool->irqs, XA_FLAGS_ALLOC);
	pool->xa_num_irqs.min = start;
	pool->xa_num_irqs.max = start + size - 1;
	if (name)
		snprintf(pool->name, MLX5_MAX_IRQ_NAME - MLX5_MAX_IRQ_IDX_CHARS,
			 "%s", name);
	pool->min_threshold = min_threshold * MLX5_EQ_REFS_PER_IRQ;
	pool->max_threshold = max_threshold * MLX5_EQ_REFS_PER_IRQ;
	mlx5_core_dbg(dev, "pool->name = %s, pool->size = %d, pool->start = %d",
		      name, size, start);
	return pool;
}

static void irq_pool_free(struct mlx5_irq_pool *pool)
{
	struct mlx5_irq *irq;
	unsigned long index;

	/* There are cases in which we are destrying the irq_table before
	 * freeing all the IRQs, fast teardown for example. Hence, free the irqs
	 * which might not have been freed.
	 */
	xa_for_each(&pool->irqs, index, irq)
		irq_release(irq);
	xa_destroy(&pool->irqs);
	mutex_destroy(&pool->lock);
	kfree(pool->irqs_per_cpu);
	kvfree(pool);
}

static int irq_pools_init(struct mlx5_core_dev *dev, int sf_vec, int pcif_vec)
{
	struct mlx5_irq_table *table = dev->priv.irq_table;
	int num_sf_ctrl_by_msix;
	int num_sf_ctrl_by_sfs;
	int num_sf_ctrl;
	int err;

	/* init pcif_pool */
	table->pcif_pool = irq_pool_alloc(dev, 0, pcif_vec, NULL,
					  MLX5_EQ_SHARE_IRQ_MIN_COMP,
					  MLX5_EQ_SHARE_IRQ_MAX_COMP);
	if (IS_ERR(table->pcif_pool))
		return PTR_ERR(table->pcif_pool);
	if (!mlx5_sf_max_functions(dev))
		return 0;
	if (sf_vec < MLX5_IRQ_VEC_COMP_BASE_SF) {
		mlx5_core_dbg(dev, "Not enught IRQs for SFs. SF may run at lower performance\n");
		return 0;
	}

	/* init sf_ctrl_pool */
	num_sf_ctrl_by_msix = DIV_ROUND_UP(sf_vec, MLX5_COMP_EQS_PER_SF);
	num_sf_ctrl_by_sfs = DIV_ROUND_UP(mlx5_sf_max_functions(dev),
					  MLX5_SFS_PER_CTRL_IRQ);
	num_sf_ctrl = min_t(int, num_sf_ctrl_by_msix, num_sf_ctrl_by_sfs);
	num_sf_ctrl = min_t(int, MLX5_IRQ_CTRL_SF_MAX, num_sf_ctrl);
	table->sf_ctrl_pool = irq_pool_alloc(dev, pcif_vec, num_sf_ctrl,
					     "mlx5_sf_ctrl",
					     MLX5_EQ_SHARE_IRQ_MIN_CTRL,
					     MLX5_EQ_SHARE_IRQ_MAX_CTRL);
	if (IS_ERR(table->sf_ctrl_pool)) {
		err = PTR_ERR(table->sf_ctrl_pool);
		goto err_pf;
	}
	/* init sf_comp_pool */
	table->sf_comp_pool = irq_pool_alloc(dev, pcif_vec + num_sf_ctrl,
					     sf_vec - num_sf_ctrl, "mlx5_sf_comp",
					     MLX5_EQ_SHARE_IRQ_MIN_COMP,
					     MLX5_EQ_SHARE_IRQ_MAX_COMP);
	if (IS_ERR(table->sf_comp_pool)) {
		err = PTR_ERR(table->sf_comp_pool);
		goto err_sf_ctrl;
	}

	table->sf_comp_pool->irqs_per_cpu = kcalloc(nr_cpu_ids, sizeof(u16), GFP_KERNEL);
	if (!table->sf_comp_pool->irqs_per_cpu) {
		err = -ENOMEM;
		goto err_irqs_per_cpu;
	}

	return 0;

err_irqs_per_cpu:
	irq_pool_free(table->sf_comp_pool);
err_sf_ctrl:
	irq_pool_free(table->sf_ctrl_pool);
err_pf:
	irq_pool_free(table->pcif_pool);
	return err;
}

static void irq_pools_destroy(struct mlx5_irq_table *table)
{
	if (table->sf_ctrl_pool) {
		irq_pool_free(table->sf_comp_pool);
		irq_pool_free(table->sf_ctrl_pool);
	}
	irq_pool_free(table->pcif_pool);
}

static void mlx5_irq_pool_free_irqs(struct mlx5_irq_pool *pool)
{
	struct mlx5_irq *irq;
	unsigned long index;

	xa_for_each(&pool->irqs, index, irq)
		mlx5_system_free_irq(irq);

}

static void mlx5_irq_pools_free_irqs(struct mlx5_irq_table *table)
{
	if (table->sf_ctrl_pool) {
		mlx5_irq_pool_free_irqs(table->sf_comp_pool);
		mlx5_irq_pool_free_irqs(table->sf_ctrl_pool);
	}
	mlx5_irq_pool_free_irqs(table->pcif_pool);
}

/* irq_table API */

int mlx5_irq_table_init(struct mlx5_core_dev *dev)
{
	struct mlx5_irq_table *irq_table;

	if (mlx5_core_is_sf(dev))
		return 0;

	irq_table = kvzalloc_node(sizeof(*irq_table), GFP_KERNEL,
				  dev->priv.numa_node);
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

int mlx5_irq_table_get_num_comp(struct mlx5_irq_table *table)
{
	if (!table->pcif_pool->xa_num_irqs.max)
		return 1;
	return table->pcif_pool->xa_num_irqs.max - table->pcif_pool->xa_num_irqs.min;
}

int mlx5_irq_table_create(struct mlx5_core_dev *dev)
{
	int num_eqs = MLX5_CAP_GEN(dev, max_num_eqs) ?
		      MLX5_CAP_GEN(dev, max_num_eqs) :
		      1 << MLX5_CAP_GEN(dev, log_max_eq);
	int total_vec;
	int pcif_vec;
	int req_vec;
	int err;
	int n;

	if (mlx5_core_is_sf(dev))
		return 0;

	pcif_vec = MLX5_CAP_GEN(dev, num_ports) * num_online_cpus() + 1;
	pcif_vec = min_t(int, pcif_vec, num_eqs);

	total_vec = pcif_vec;
	if (mlx5_sf_max_functions(dev))
		total_vec += MLX5_IRQ_CTRL_SF_MAX +
			MLX5_COMP_EQS_PER_SF * mlx5_sf_max_functions(dev);
	total_vec = min_t(int, total_vec, pci_msix_vec_count(dev->pdev));
	pcif_vec = min_t(int, pcif_vec, pci_msix_vec_count(dev->pdev));

	req_vec = pci_msix_can_alloc_dyn(dev->pdev) ? 1 : total_vec;
	n = pci_alloc_irq_vectors(dev->pdev, 1, req_vec, PCI_IRQ_MSIX);
	if (n < 0)
		return n;

	err = irq_pools_init(dev, total_vec - pcif_vec, pcif_vec);
	if (err)
		pci_free_irq_vectors(dev->pdev);

	return err;
}

void mlx5_irq_table_destroy(struct mlx5_core_dev *dev)
{
	struct mlx5_irq_table *table = dev->priv.irq_table;

	if (mlx5_core_is_sf(dev))
		return;

	/* There are cases where IRQs still will be in used when we reaching
	 * to here. Hence, making sure all the irqs are released.
	 */
	irq_pools_destroy(table);
	pci_free_irq_vectors(dev->pdev);
}

void mlx5_irq_table_free_irqs(struct mlx5_core_dev *dev)
{
	struct mlx5_irq_table *table = dev->priv.irq_table;

	if (mlx5_core_is_sf(dev))
		return;

	mlx5_irq_pools_free_irqs(table);
	pci_free_irq_vectors(dev->pdev);
}

int mlx5_irq_table_get_sfs_vec(struct mlx5_irq_table *table)
{
	if (table->sf_comp_pool)
		return min_t(int, num_online_cpus(),
			     table->sf_comp_pool->xa_num_irqs.max -
			     table->sf_comp_pool->xa_num_irqs.min + 1);
	else
		return mlx5_irq_table_get_num_comp(table);
}

struct mlx5_irq_table *mlx5_irq_table_get(struct mlx5_core_dev *dev)
{
#ifdef CONFIG_MLX5_SF
	if (mlx5_core_is_sf(dev))
		return dev->priv.parent_mdev->priv.irq_table;
#endif
	return dev->priv.irq_table;
}

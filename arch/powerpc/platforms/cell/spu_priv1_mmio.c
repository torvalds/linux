/*
 * access to SPU privileged registers
 */
#include <linux/module.h>

#include <asm/io.h>
#include <asm/spu.h>

void spu_int_mask_and(struct spu *spu, int class, u64 mask)
{
	u64 old_mask;

	old_mask = in_be64(&spu->priv1->int_mask_RW[class]);
	out_be64(&spu->priv1->int_mask_RW[class], old_mask & mask);
}
EXPORT_SYMBOL_GPL(spu_int_mask_and);

void spu_int_mask_or(struct spu *spu, int class, u64 mask)
{
	u64 old_mask;

	old_mask = in_be64(&spu->priv1->int_mask_RW[class]);
	out_be64(&spu->priv1->int_mask_RW[class], old_mask | mask);
}
EXPORT_SYMBOL_GPL(spu_int_mask_or);

void spu_int_mask_set(struct spu *spu, int class, u64 mask)
{
	out_be64(&spu->priv1->int_mask_RW[class], mask);
}
EXPORT_SYMBOL_GPL(spu_int_mask_set);

u64 spu_int_mask_get(struct spu *spu, int class)
{
	return in_be64(&spu->priv1->int_mask_RW[class]);
}
EXPORT_SYMBOL_GPL(spu_int_mask_get);

void spu_int_stat_clear(struct spu *spu, int class, u64 stat)
{
	out_be64(&spu->priv1->int_stat_RW[class], stat);
}
EXPORT_SYMBOL_GPL(spu_int_stat_clear);

u64 spu_int_stat_get(struct spu *spu, int class)
{
	return in_be64(&spu->priv1->int_stat_RW[class]);
}
EXPORT_SYMBOL_GPL(spu_int_stat_get);

void spu_int_route_set(struct spu *spu, u64 route)
{
	out_be64(&spu->priv1->int_route_RW, route);
}
EXPORT_SYMBOL_GPL(spu_int_route_set);

u64 spu_mfc_dar_get(struct spu *spu)
{
	return in_be64(&spu->priv1->mfc_dar_RW);
}
EXPORT_SYMBOL_GPL(spu_mfc_dar_get);

u64 spu_mfc_dsisr_get(struct spu *spu)
{
	return in_be64(&spu->priv1->mfc_dsisr_RW);
}
EXPORT_SYMBOL_GPL(spu_mfc_dsisr_get);

void spu_mfc_dsisr_set(struct spu *spu, u64 dsisr)
{
	out_be64(&spu->priv1->mfc_dsisr_RW, dsisr);
}
EXPORT_SYMBOL_GPL(spu_mfc_dsisr_set);

void spu_mfc_sdr_set(struct spu *spu, u64 sdr)
{
	out_be64(&spu->priv1->mfc_sdr_RW, sdr);
}
EXPORT_SYMBOL_GPL(spu_mfc_sdr_set);

void spu_mfc_sr1_set(struct spu *spu, u64 sr1)
{
	out_be64(&spu->priv1->mfc_sr1_RW, sr1);
}
EXPORT_SYMBOL_GPL(spu_mfc_sr1_set);

u64 spu_mfc_sr1_get(struct spu *spu)
{
	return in_be64(&spu->priv1->mfc_sr1_RW);
}
EXPORT_SYMBOL_GPL(spu_mfc_sr1_get);

void spu_mfc_tclass_id_set(struct spu *spu, u64 tclass_id)
{
	out_be64(&spu->priv1->mfc_tclass_id_RW, tclass_id);
}
EXPORT_SYMBOL_GPL(spu_mfc_tclass_id_set);

u64 spu_mfc_tclass_id_get(struct spu *spu)
{
	return in_be64(&spu->priv1->mfc_tclass_id_RW);
}
EXPORT_SYMBOL_GPL(spu_mfc_tclass_id_get);

void spu_tlb_invalidate(struct spu *spu)
{
	out_be64(&spu->priv1->tlb_invalidate_entry_W, 0ul);
}
EXPORT_SYMBOL_GPL(spu_tlb_invalidate);

void spu_resource_allocation_groupID_set(struct spu *spu, u64 id)
{
	out_be64(&spu->priv1->resource_allocation_groupID_RW, id);
}
EXPORT_SYMBOL_GPL(spu_resource_allocation_groupID_set);

u64 spu_resource_allocation_groupID_get(struct spu *spu)
{
	return in_be64(&spu->priv1->resource_allocation_groupID_RW);
}
EXPORT_SYMBOL_GPL(spu_resource_allocation_groupID_get);

void spu_resource_allocation_enable_set(struct spu *spu, u64 enable)
{
	out_be64(&spu->priv1->resource_allocation_enable_RW, enable);
}
EXPORT_SYMBOL_GPL(spu_resource_allocation_enable_set);

u64 spu_resource_allocation_enable_get(struct spu *spu)
{
	return in_be64(&spu->priv1->resource_allocation_enable_RW);
}
EXPORT_SYMBOL_GPL(spu_resource_allocation_enable_get);

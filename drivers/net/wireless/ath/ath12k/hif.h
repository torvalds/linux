/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2019-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef ATH12K_HIF_H
#define ATH12K_HIF_H

#include "core.h"

struct ath12k_hif_ops {
	u32 (*read32)(struct ath12k_base *sc, u32 address);
	void (*write32)(struct ath12k_base *sc, u32 address, u32 data);
	void (*irq_enable)(struct ath12k_base *sc);
	void (*irq_disable)(struct ath12k_base *sc);
	int (*start)(struct ath12k_base *sc);
	void (*stop)(struct ath12k_base *sc);
	int (*power_up)(struct ath12k_base *sc);
	void (*power_down)(struct ath12k_base *sc);
	int (*suspend)(struct ath12k_base *ab);
	int (*resume)(struct ath12k_base *ab);
	int (*map_service_to_pipe)(struct ath12k_base *sc, u16 service_id,
				   u8 *ul_pipe, u8 *dl_pipe);
	int (*get_user_msi_vector)(struct ath12k_base *ab, char *user_name,
				   int *num_vectors, u32 *user_base_data,
				   u32 *base_vector);
	void (*get_msi_address)(struct ath12k_base *ab, u32 *msi_addr_lo,
				u32 *msi_addr_hi);
	void (*ce_irq_enable)(struct ath12k_base *ab);
	void (*ce_irq_disable)(struct ath12k_base *ab);
	void (*get_ce_msi_idx)(struct ath12k_base *ab, u32 ce_id, u32 *msi_idx);
};

static inline int ath12k_hif_map_service_to_pipe(struct ath12k_base *ab, u16 service_id,
						 u8 *ul_pipe, u8 *dl_pipe)
{
	return ab->hif.ops->map_service_to_pipe(ab, service_id,
						ul_pipe, dl_pipe);
}

static inline int ath12k_hif_get_user_msi_vector(struct ath12k_base *ab,
						 char *user_name,
						 int *num_vectors,
						 u32 *user_base_data,
						 u32 *base_vector)
{
	if (!ab->hif.ops->get_user_msi_vector)
		return -EOPNOTSUPP;

	return ab->hif.ops->get_user_msi_vector(ab, user_name, num_vectors,
						user_base_data,
						base_vector);
}

static inline void ath12k_hif_get_msi_address(struct ath12k_base *ab,
					      u32 *msi_addr_lo,
					      u32 *msi_addr_hi)
{
	if (!ab->hif.ops->get_msi_address)
		return;

	ab->hif.ops->get_msi_address(ab, msi_addr_lo, msi_addr_hi);
}

static inline void ath12k_hif_get_ce_msi_idx(struct ath12k_base *ab, u32 ce_id,
					     u32 *msi_data_idx)
{
	if (ab->hif.ops->get_ce_msi_idx)
		ab->hif.ops->get_ce_msi_idx(ab, ce_id, msi_data_idx);
	else
		*msi_data_idx = ce_id;
}

static inline void ath12k_hif_ce_irq_enable(struct ath12k_base *ab)
{
	if (ab->hif.ops->ce_irq_enable)
		ab->hif.ops->ce_irq_enable(ab);
}

static inline void ath12k_hif_ce_irq_disable(struct ath12k_base *ab)
{
	if (ab->hif.ops->ce_irq_disable)
		ab->hif.ops->ce_irq_disable(ab);
}

static inline void ath12k_hif_irq_enable(struct ath12k_base *ab)
{
	ab->hif.ops->irq_enable(ab);
}

static inline void ath12k_hif_irq_disable(struct ath12k_base *ab)
{
	ab->hif.ops->irq_disable(ab);
}

static inline int ath12k_hif_suspend(struct ath12k_base *ab)
{
	if (ab->hif.ops->suspend)
		return ab->hif.ops->suspend(ab);

	return 0;
}

static inline int ath12k_hif_resume(struct ath12k_base *ab)
{
	if (ab->hif.ops->resume)
		return ab->hif.ops->resume(ab);

	return 0;
}

static inline int ath12k_hif_start(struct ath12k_base *ab)
{
	return ab->hif.ops->start(ab);
}

static inline void ath12k_hif_stop(struct ath12k_base *ab)
{
	ab->hif.ops->stop(ab);
}

static inline u32 ath12k_hif_read32(struct ath12k_base *ab, u32 address)
{
	return ab->hif.ops->read32(ab, address);
}

static inline void ath12k_hif_write32(struct ath12k_base *ab, u32 address,
				      u32 data)
{
	ab->hif.ops->write32(ab, address, data);
}

static inline int ath12k_hif_power_up(struct ath12k_base *ab)
{
	return ab->hif.ops->power_up(ab);
}

static inline void ath12k_hif_power_down(struct ath12k_base *ab)
{
	ab->hif.ops->power_down(ab);
}

#endif /* ATH12K_HIF_H */

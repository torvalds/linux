/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * DCN4.2 IRQ service interface (dal-dev only)
 */
#ifndef __IRQ_SERVICE_DCN42_H__
#define __IRQ_SERVICE_DCN42_H__

#include "../dce110/irq_service_dce110.h"

struct irq_service *dal_irq_service_dcn42_create(
	struct irq_service_init_data *init_data);

#endif /* __IRQ_SERVICE_DCN42_H__ */

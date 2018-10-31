// SPDX-License-Identifier: GPL-2.0-or-later
//
// Actions Semi Owl SoCs Reset Management Unit driver
//
// Copyright (c) 2018 Linaro Ltd.
// Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>

#ifndef _OWL_RESET_H_
#define _OWL_RESET_H_

#include <linux/reset-controller.h>

struct owl_reset_map {
	u32	reg;
	u32	bit;
};

struct owl_reset {
	struct reset_controller_dev	rcdev;
	const struct owl_reset_map	*reset_map;
	struct regmap			*regmap;
};

static inline struct owl_reset *to_owl_reset(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct owl_reset, rcdev);
}

extern const struct reset_control_ops owl_reset_ops;

#endif /* _OWL_RESET_H_ */

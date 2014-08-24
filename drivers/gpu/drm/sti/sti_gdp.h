/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Benjamin Gaignard <benjamin.gaignard@st.com>
 *          Fabien Dessenne <fabien.dessenne@st.com>
 *          for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef _STI_GDP_H_
#define _STI_GDP_H_

#include <linux/types.h>

struct sti_layer *sti_gdp_create(struct device *dev, int id);

#endif

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef FXOS8700_H_
#define FXOS8700_H_

extern const struct regmap_config fxos8700_regmap_config;

int fxos8700_core_probe(struct device *dev, struct regmap *regmap,
			const char *name, bool use_spi);

#endif  /* FXOS8700_H_ */

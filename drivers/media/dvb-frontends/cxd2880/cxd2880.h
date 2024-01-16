/* SPDX-License-Identifier: GPL-2.0 */
/*
 * cxd2880.h
 * Sony CXD2880 DVB-T2/T tuner + demodulator driver public definitions
 *
 * Copyright (C) 2016, 2017, 2018 Sony Semiconductor Solutions Corporation
 */

#ifndef CXD2880_H
#define CXD2880_H

struct cxd2880_config {
	struct spi_device *spi;
	struct mutex *spi_mutex; /* For SPI access exclusive control */
};

#if IS_REACHABLE(CONFIG_DVB_CXD2880)
extern struct dvb_frontend *cxd2880_attach(struct dvb_frontend *fe,
					struct cxd2880_config *cfg);
#else
static inline struct dvb_frontend *cxd2880_attach(struct dvb_frontend *fe,
					struct cxd2880_config *cfg)
{
	pr_warn("%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_CXD2880 */

#endif /* CXD2880_H */

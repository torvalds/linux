/*
 * Abilis Systems Single DVB-T Receiver
 * Copyright (C) 2014 Mauro Carvalho Chehab <mchehab+samsung@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "as102_fe_types.h"

struct as102_fe_ops {
	int (*set_tune)(void *priv, struct as10x_tune_args *tune_args);
	int (*get_tps)(void *priv, struct as10x_tps *tps);
	int (*get_status)(void *priv, struct as10x_tune_status *tstate);
	int (*get_stats)(void *priv, struct as10x_demod_stats *demod_stats);
	int (*stream_ctrl)(void *priv, int acquire, uint32_t elna_cfg);
};

struct dvb_frontend *as102_attach(const char *name,
				  const struct as102_fe_ops *ops,
				  void *priv,
				  uint8_t elna_cfg);

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#ifndef __MEDIA_RPPX1_H__
#define __MEDIA_RPPX1_H__

#include <linux/device.h>
#include <linux/types.h>

#include "rpp_module.h"

#define RPPX1_IRQ_ID_256HIST			BIT(27)
#define RPPX1_IRQ_ID_PRE2_DPCC			BIT(25)
#define RPPX1_IRQ_ID_PRE1_DPCC			BIT(24)
#define RPPX1_IRQ_ID_MV_OUT_FRAME_OUT		BIT(23)
#define RPPX1_IRQ_ID_MV_OUT_OFF			BIT(22)
#define RPPX1_IRQ_ID_POST_AWB_MEAS		BIT(21)
#define RPPX1_IRQ_ID_POST_HIST_MEAS		BIT(20)
#define RPPX1_IRQ_ID_POST_TM			BIT(19)
#define RPPX1_IRQ_ID_PRE1_EXM			BIT(18)
#define RPPX1_IRQ_ID_PRE1_HIST			BIT(17)
#define RPPX1_IRQ_ID_PRE1_FRAME_IN		BIT(16)
#define RPPX1_IRQ_ID_PRE1_HSTART		BIT(15)
#define RPPX1_IRQ_ID_PRE1_VSTART		BIT(14)
#define RPPX1_IRQ_ID_PRE2_EXM			BIT(13)
#define RPPX1_IRQ_ID_PRE2_HIST			BIT(12)
#define RPPX1_IRQ_ID_PRE2_FRAME_IN		BIT(11)
#define RPPX1_IRQ_ID_PRE2_HSTART		BIT(10)
#define RPPX1_IRQ_ID_PRE2_VSTART		BIT(9)
#define RPPX1_IRQ_ID_OUT_FRAME			BIT(3)
#define RPPX1_IRQ_ID_OUT_OFF			BIT(2)
#define RPPX1_IRQ_ID_RMAP_MEAS			BIT(1)
#define RPPX1_IRQ_ID_RMAP_DONE			BIT(0)

struct rppx1 {
	struct device *dev;
	void __iomem *base;

	struct {
		struct rpp_module acq;
		struct rpp_module bls;
		struct rpp_module lin;
		struct rpp_module lsc;
		struct rpp_module awbg;
		struct rpp_module dpcc;
		struct rpp_module bd;
		struct rpp_module hist;
		struct rpp_module hist256;
		struct rpp_module exm;
	} pre1;

	struct {
		struct rpp_module acq;
		struct rpp_module bls;
		struct rpp_module lin;
		struct rpp_module lsc;
		struct rpp_module awbg;
		struct rpp_module dpcc;
		struct rpp_module bd;
		struct rpp_module hist;
		struct rpp_module exm;
	} pre2;

	struct {
		struct rpp_module awbg;
		struct rpp_module ccor;
		struct rpp_module hist;
		struct rpp_module db;
		struct rpp_module cac;
		struct rpp_module ltm;
		struct rpp_module ltmmeas;
		struct rpp_module wbmeas;
		struct rpp_module bdrgb;
		struct rpp_module shrp;
	} post;

	struct {
		struct rpp_module ga;
		struct rpp_module is;
		struct rpp_module ccor;
		struct rpp_module outif;
		struct rpp_module outregs;
	} hv;

	struct {
		struct rpp_module ga;
		struct rpp_module is;
		struct rpp_module ccor;
		struct rpp_module outif;
		struct rpp_module outregs;
		struct rpp_module xyz2luv;
	} mv;

	struct rpp_module rmap;
	struct rpp_module rmapmeas;
};

void rppx1_write(struct rppx1 *rpp, u32 offset, u32 value);
u32 rppx1_read(struct rppx1 *rpp, u32 offset);

#endif /* __MEDIA_RPPX1_H__ */

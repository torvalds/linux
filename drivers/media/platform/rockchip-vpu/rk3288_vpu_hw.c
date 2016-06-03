/*
 * Rockchip VPU codec driver
 *
 * Copyright (C) 2016 Rockchip Electronics Co., Ltd.
 *	Jeffy Chen <jeffy.chen@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "rockchip_vpu_common.h"

#include "rk3288_vpu_regs.h"

/*
 * Interrupt handlers.
 */

int rk3288_vpu_enc_irq(int irq, struct rockchip_vpu_dev *vpu)
{
	u32 status = vepu_read(vpu, VEPU_REG_INTERRUPT);

	vepu_write(vpu, 0, VEPU_REG_INTERRUPT);

	if (status & VEPU_REG_INTERRUPT_BIT) {
		vepu_write(vpu, 0, VEPU_REG_AXI_CTRL);
		return 0;
	}

	return -1;
}

int rk3288_vpu_dec_irq(int irq, struct rockchip_vpu_dev *vpu)
{
	u32 status = vdpu_read(vpu, VDPU_REG_INTERRUPT);

	vdpu_write(vpu, 0, VDPU_REG_INTERRUPT);

	vpu_debug(3, "vdpu_irq status: %08x\n", status);

	if (status & VDPU_REG_INTERRUPT_DEC_IRQ) {
		vdpu_write(vpu, 0, VDPU_REG_CONFIG);
		return 0;
	}

	return -1;
}

/*
 * Initialization/clean-up.
 */

void rk3288_vpu_enc_reset(struct rockchip_vpu_ctx *ctx)
{
	struct rockchip_vpu_dev *vpu = ctx->dev;

	vepu_write(vpu, VEPU_REG_INTERRUPT_DIS_BIT, VEPU_REG_INTERRUPT);
	vepu_write(vpu, 0, VEPU_REG_ENC_CTRL);
	vepu_write(vpu, 0, VEPU_REG_AXI_CTRL);
}

void rk3288_vpu_dec_reset(struct rockchip_vpu_ctx *ctx)
{
	struct rockchip_vpu_dev *vpu = ctx->dev;

	vdpu_write(vpu, VDPU_REG_INTERRUPT_DEC_IRQ_DIS, VDPU_REG_INTERRUPT);
	vdpu_write(vpu, 0, VDPU_REG_CONFIG);
}

/*
 * Copyright 2007-8 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 */
#include "drmP.h"
#include "radeon_drm.h"
#include "radeon.h"
#include "atom.h"

/**
 * radeon_ddc_probe
 *
 */
bool radeon_ddc_probe(struct radeon_connector *radeon_connector)
{
	u8 out_buf[] = { 0x0, 0x0};
	u8 buf[2];
	int ret;
	struct i2c_msg msgs[] = {
		{
			.addr = 0x50,
			.flags = 0,
			.len = 1,
			.buf = out_buf,
		},
		{
			.addr = 0x50,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = buf,
		}
	};

	ret = i2c_transfer(&radeon_connector->ddc_bus->adapter, msgs, 2);
	if (ret == 2)
		return true;

	return false;
}

/* bit banging i2c */

static void radeon_i2c_do_lock(struct radeon_i2c_chan *i2c, int lock_state)
{
	struct radeon_device *rdev = i2c->dev->dev_private;
	struct radeon_i2c_bus_rec *rec = &i2c->rec;
	uint32_t temp;

	/* RV410 appears to have a bug where the hw i2c in reset
	 * holds the i2c port in a bad state - switch hw i2c away before
	 * doing DDC - do this for all r200s/r300s/r400s for safety sake
	 */
	if (rec->hw_capable) {
		if ((rdev->family >= CHIP_R200) && !ASIC_IS_AVIVO(rdev)) {
			u32 reg;

			if (rdev->family >= CHIP_RV350)
				reg = RADEON_GPIO_MONID;
			else if ((rdev->family == CHIP_R300) ||
				 (rdev->family == CHIP_R350))
				reg = RADEON_GPIO_DVI_DDC;
			else
				reg = RADEON_GPIO_CRT2_DDC;

			mutex_lock(&rdev->dc_hw_i2c_mutex);
			if (rec->a_clk_reg == reg) {
				WREG32(RADEON_DVI_I2C_CNTL_0, (RADEON_I2C_SOFT_RST |
							       R200_DVI_I2C_PIN_SEL(R200_SEL_DDC1)));
			} else {
				WREG32(RADEON_DVI_I2C_CNTL_0, (RADEON_I2C_SOFT_RST |
							       R200_DVI_I2C_PIN_SEL(R200_SEL_DDC3)));
			}
			mutex_unlock(&rdev->dc_hw_i2c_mutex);
		}
	}

	/* clear the output pin values */
	temp = RREG32(rec->a_clk_reg) & ~rec->a_clk_mask;
	WREG32(rec->a_clk_reg, temp);

	temp = RREG32(rec->a_data_reg) & ~rec->a_data_mask;
	WREG32(rec->a_data_reg, temp);

	/* set the pins to input */
	temp = RREG32(rec->en_clk_reg) & ~rec->en_clk_mask;
	WREG32(rec->en_clk_reg, temp);

	temp = RREG32(rec->en_data_reg) & ~rec->en_data_mask;
	WREG32(rec->en_data_reg, temp);

	/* mask the gpio pins for software use */
	temp = RREG32(rec->mask_clk_reg);
	if (lock_state)
		temp |= rec->mask_clk_mask;
	else
		temp &= ~rec->mask_clk_mask;
	WREG32(rec->mask_clk_reg, temp);
	temp = RREG32(rec->mask_clk_reg);

	temp = RREG32(rec->mask_data_reg);
	if (lock_state)
		temp |= rec->mask_data_mask;
	else
		temp &= ~rec->mask_data_mask;
	WREG32(rec->mask_data_reg, temp);
	temp = RREG32(rec->mask_data_reg);
}

static int get_clock(void *i2c_priv)
{
	struct radeon_i2c_chan *i2c = i2c_priv;
	struct radeon_device *rdev = i2c->dev->dev_private;
	struct radeon_i2c_bus_rec *rec = &i2c->rec;
	uint32_t val;

	/* read the value off the pin */
	val = RREG32(rec->y_clk_reg);
	val &= rec->y_clk_mask;

	return (val != 0);
}


static int get_data(void *i2c_priv)
{
	struct radeon_i2c_chan *i2c = i2c_priv;
	struct radeon_device *rdev = i2c->dev->dev_private;
	struct radeon_i2c_bus_rec *rec = &i2c->rec;
	uint32_t val;

	/* read the value off the pin */
	val = RREG32(rec->y_data_reg);
	val &= rec->y_data_mask;

	return (val != 0);
}

static void set_clock(void *i2c_priv, int clock)
{
	struct radeon_i2c_chan *i2c = i2c_priv;
	struct radeon_device *rdev = i2c->dev->dev_private;
	struct radeon_i2c_bus_rec *rec = &i2c->rec;
	uint32_t val;

	/* set pin direction */
	val = RREG32(rec->en_clk_reg) & ~rec->en_clk_mask;
	val |= clock ? 0 : rec->en_clk_mask;
	WREG32(rec->en_clk_reg, val);
}

static void set_data(void *i2c_priv, int data)
{
	struct radeon_i2c_chan *i2c = i2c_priv;
	struct radeon_device *rdev = i2c->dev->dev_private;
	struct radeon_i2c_bus_rec *rec = &i2c->rec;
	uint32_t val;

	/* set pin direction */
	val = RREG32(rec->en_data_reg) & ~rec->en_data_mask;
	val |= data ? 0 : rec->en_data_mask;
	WREG32(rec->en_data_reg, val);
}

static int pre_xfer(struct i2c_adapter *i2c_adap)
{
	struct radeon_i2c_chan *i2c = i2c_get_adapdata(i2c_adap);

	radeon_i2c_do_lock(i2c, 1);

	return 0;
}

static void post_xfer(struct i2c_adapter *i2c_adap)
{
	struct radeon_i2c_chan *i2c = i2c_get_adapdata(i2c_adap);

	radeon_i2c_do_lock(i2c, 0);
}

/* hw i2c */

static u32 radeon_get_i2c_prescale(struct radeon_device *rdev)
{
	u32 sclk = radeon_get_engine_clock(rdev);
	u32 prescale = 0;
	u32 nm;
	u8 n, m, loop;
	int i2c_clock;

	switch (rdev->family) {
	case CHIP_R100:
	case CHIP_RV100:
	case CHIP_RS100:
	case CHIP_RV200:
	case CHIP_RS200:
	case CHIP_R200:
	case CHIP_RV250:
	case CHIP_RS300:
	case CHIP_RV280:
	case CHIP_R300:
	case CHIP_R350:
	case CHIP_RV350:
		i2c_clock = 60;
		nm = (sclk * 10) / (i2c_clock * 4);
		for (loop = 1; loop < 255; loop++) {
			if ((nm / loop) < loop)
				break;
		}
		n = loop - 1;
		m = loop - 2;
		prescale = m | (n << 8);
		break;
	case CHIP_RV380:
	case CHIP_RS400:
	case CHIP_RS480:
	case CHIP_R420:
	case CHIP_R423:
	case CHIP_RV410:
		prescale = (((sclk * 10)/(4 * 128 * 100) + 1) << 8) + 128;
		break;
	case CHIP_RS600:
	case CHIP_RS690:
	case CHIP_RS740:
		/* todo */
		break;
	case CHIP_RV515:
	case CHIP_R520:
	case CHIP_RV530:
	case CHIP_RV560:
	case CHIP_RV570:
	case CHIP_R580:
		i2c_clock = 50;
		if (rdev->family == CHIP_R520)
			prescale = (127 << 8) + ((sclk * 10) / (4 * 127 * i2c_clock));
		else
			prescale = (((sclk * 10)/(4 * 128 * 100) + 1) << 8) + 128;
		break;
	case CHIP_R600:
	case CHIP_RV610:
	case CHIP_RV630:
	case CHIP_RV670:
		/* todo */
		break;
	case CHIP_RV620:
	case CHIP_RV635:
	case CHIP_RS780:
	case CHIP_RS880:
	case CHIP_RV770:
	case CHIP_RV730:
	case CHIP_RV710:
	case CHIP_RV740:
		/* todo */
		break;
	case CHIP_CEDAR:
	case CHIP_REDWOOD:
	case CHIP_JUNIPER:
	case CHIP_CYPRESS:
	case CHIP_HEMLOCK:
		/* todo */
		break;
	default:
		DRM_ERROR("i2c: unhandled radeon chip\n");
		break;
	}
	return prescale;
}


/* hw i2c engine for r1xx-4xx hardware
 * hw can buffer up to 15 bytes
 */
static int r100_hw_i2c_xfer(struct i2c_adapter *i2c_adap,
			    struct i2c_msg *msgs, int num)
{
	struct radeon_i2c_chan *i2c = i2c_get_adapdata(i2c_adap);
	struct radeon_device *rdev = i2c->dev->dev_private;
	struct radeon_i2c_bus_rec *rec = &i2c->rec;
	struct i2c_msg *p;
	int i, j, k, ret = num;
	u32 prescale;
	u32 i2c_cntl_0, i2c_cntl_1, i2c_data;
	u32 tmp, reg;

	mutex_lock(&rdev->dc_hw_i2c_mutex);
	/* take the pm lock since we need a constant sclk */
	mutex_lock(&rdev->pm.mutex);

	prescale = radeon_get_i2c_prescale(rdev);

	reg = ((prescale << RADEON_I2C_PRESCALE_SHIFT) |
	       RADEON_I2C_DRIVE_EN |
	       RADEON_I2C_START |
	       RADEON_I2C_STOP |
	       RADEON_I2C_GO);

	if (rdev->is_atom_bios) {
		tmp = RREG32(RADEON_BIOS_6_SCRATCH);
		WREG32(RADEON_BIOS_6_SCRATCH, tmp | ATOM_S6_HW_I2C_BUSY_STATE);
	}

	if (rec->mm_i2c) {
		i2c_cntl_0 = RADEON_I2C_CNTL_0;
		i2c_cntl_1 = RADEON_I2C_CNTL_1;
		i2c_data = RADEON_I2C_DATA;
	} else {
		i2c_cntl_0 = RADEON_DVI_I2C_CNTL_0;
		i2c_cntl_1 = RADEON_DVI_I2C_CNTL_1;
		i2c_data = RADEON_DVI_I2C_DATA;

		switch (rdev->family) {
		case CHIP_R100:
		case CHIP_RV100:
		case CHIP_RS100:
		case CHIP_RV200:
		case CHIP_RS200:
		case CHIP_RS300:
			switch (rec->mask_clk_reg) {
			case RADEON_GPIO_DVI_DDC:
				/* no gpio select bit */
				break;
			default:
				DRM_ERROR("gpio not supported with hw i2c\n");
				ret = -EINVAL;
				goto done;
			}
			break;
		case CHIP_R200:
			/* only bit 4 on r200 */
			switch (rec->mask_clk_reg) {
			case RADEON_GPIO_DVI_DDC:
				reg |= R200_DVI_I2C_PIN_SEL(R200_SEL_DDC1);
				break;
			case RADEON_GPIO_MONID:
				reg |= R200_DVI_I2C_PIN_SEL(R200_SEL_DDC3);
				break;
			default:
				DRM_ERROR("gpio not supported with hw i2c\n");
				ret = -EINVAL;
				goto done;
			}
			break;
		case CHIP_RV250:
		case CHIP_RV280:
			/* bits 3 and 4 */
			switch (rec->mask_clk_reg) {
			case RADEON_GPIO_DVI_DDC:
				reg |= R200_DVI_I2C_PIN_SEL(R200_SEL_DDC1);
				break;
			case RADEON_GPIO_VGA_DDC:
				reg |= R200_DVI_I2C_PIN_SEL(R200_SEL_DDC2);
				break;
			case RADEON_GPIO_CRT2_DDC:
				reg |= R200_DVI_I2C_PIN_SEL(R200_SEL_DDC3);
				break;
			default:
				DRM_ERROR("gpio not supported with hw i2c\n");
				ret = -EINVAL;
				goto done;
			}
			break;
		case CHIP_R300:
		case CHIP_R350:
			/* only bit 4 on r300/r350 */
			switch (rec->mask_clk_reg) {
			case RADEON_GPIO_VGA_DDC:
				reg |= R200_DVI_I2C_PIN_SEL(R200_SEL_DDC1);
				break;
			case RADEON_GPIO_DVI_DDC:
				reg |= R200_DVI_I2C_PIN_SEL(R200_SEL_DDC3);
				break;
			default:
				DRM_ERROR("gpio not supported with hw i2c\n");
				ret = -EINVAL;
				goto done;
			}
			break;
		case CHIP_RV350:
		case CHIP_RV380:
		case CHIP_R420:
		case CHIP_R423:
		case CHIP_RV410:
		case CHIP_RS400:
		case CHIP_RS480:
			/* bits 3 and 4 */
			switch (rec->mask_clk_reg) {
			case RADEON_GPIO_VGA_DDC:
				reg |= R200_DVI_I2C_PIN_SEL(R200_SEL_DDC1);
				break;
			case RADEON_GPIO_DVI_DDC:
				reg |= R200_DVI_I2C_PIN_SEL(R200_SEL_DDC2);
				break;
			case RADEON_GPIO_MONID:
				reg |= R200_DVI_I2C_PIN_SEL(R200_SEL_DDC3);
				break;
			default:
				DRM_ERROR("gpio not supported with hw i2c\n");
				ret = -EINVAL;
				goto done;
			}
			break;
		default:
			DRM_ERROR("unsupported asic\n");
			ret = -EINVAL;
			goto done;
			break;
		}
	}

	/* check for bus probe */
	p = &msgs[0];
	if ((num == 1) && (p->len == 0)) {
		WREG32(i2c_cntl_0, (RADEON_I2C_DONE |
				    RADEON_I2C_NACK |
				    RADEON_I2C_HALT |
				    RADEON_I2C_SOFT_RST));
		WREG32(i2c_data, (p->addr << 1) & 0xff);
		WREG32(i2c_data, 0);
		WREG32(i2c_cntl_1, ((1 << RADEON_I2C_DATA_COUNT_SHIFT) |
				    (1 << RADEON_I2C_ADDR_COUNT_SHIFT) |
				    RADEON_I2C_EN |
				    (48 << RADEON_I2C_TIME_LIMIT_SHIFT)));
		WREG32(i2c_cntl_0, reg);
		for (k = 0; k < 32; k++) {
			udelay(10);
			tmp = RREG32(i2c_cntl_0);
			if (tmp & RADEON_I2C_GO)
				continue;
			tmp = RREG32(i2c_cntl_0);
			if (tmp & RADEON_I2C_DONE)
				break;
			else {
				DRM_DEBUG("i2c write error 0x%08x\n", tmp);
				WREG32(i2c_cntl_0, tmp | RADEON_I2C_ABORT);
				ret = -EIO;
				goto done;
			}
		}
		goto done;
	}

	for (i = 0; i < num; i++) {
		p = &msgs[i];
		for (j = 0; j < p->len; j++) {
			if (p->flags & I2C_M_RD) {
				WREG32(i2c_cntl_0, (RADEON_I2C_DONE |
						    RADEON_I2C_NACK |
						    RADEON_I2C_HALT |
						    RADEON_I2C_SOFT_RST));
				WREG32(i2c_data, ((p->addr << 1) & 0xff) | 0x1);
				WREG32(i2c_cntl_1, ((1 << RADEON_I2C_DATA_COUNT_SHIFT) |
						    (1 << RADEON_I2C_ADDR_COUNT_SHIFT) |
						    RADEON_I2C_EN |
						    (48 << RADEON_I2C_TIME_LIMIT_SHIFT)));
				WREG32(i2c_cntl_0, reg | RADEON_I2C_RECEIVE);
				for (k = 0; k < 32; k++) {
					udelay(10);
					tmp = RREG32(i2c_cntl_0);
					if (tmp & RADEON_I2C_GO)
						continue;
					tmp = RREG32(i2c_cntl_0);
					if (tmp & RADEON_I2C_DONE)
						break;
					else {
						DRM_DEBUG("i2c read error 0x%08x\n", tmp);
						WREG32(i2c_cntl_0, tmp | RADEON_I2C_ABORT);
						ret = -EIO;
						goto done;
					}
				}
				p->buf[j] = RREG32(i2c_data) & 0xff;
			} else {
				WREG32(i2c_cntl_0, (RADEON_I2C_DONE |
						    RADEON_I2C_NACK |
						    RADEON_I2C_HALT |
						    RADEON_I2C_SOFT_RST));
				WREG32(i2c_data, (p->addr << 1) & 0xff);
				WREG32(i2c_data, p->buf[j]);
				WREG32(i2c_cntl_1, ((1 << RADEON_I2C_DATA_COUNT_SHIFT) |
						    (1 << RADEON_I2C_ADDR_COUNT_SHIFT) |
						    RADEON_I2C_EN |
						    (48 << RADEON_I2C_TIME_LIMIT_SHIFT)));
				WREG32(i2c_cntl_0, reg);
				for (k = 0; k < 32; k++) {
					udelay(10);
					tmp = RREG32(i2c_cntl_0);
					if (tmp & RADEON_I2C_GO)
						continue;
					tmp = RREG32(i2c_cntl_0);
					if (tmp & RADEON_I2C_DONE)
						break;
					else {
						DRM_DEBUG("i2c write error 0x%08x\n", tmp);
						WREG32(i2c_cntl_0, tmp | RADEON_I2C_ABORT);
						ret = -EIO;
						goto done;
					}
				}
			}
		}
	}

done:
	WREG32(i2c_cntl_0, 0);
	WREG32(i2c_cntl_1, 0);
	WREG32(i2c_cntl_0, (RADEON_I2C_DONE |
			    RADEON_I2C_NACK |
			    RADEON_I2C_HALT |
			    RADEON_I2C_SOFT_RST));

	if (rdev->is_atom_bios) {
		tmp = RREG32(RADEON_BIOS_6_SCRATCH);
		tmp &= ~ATOM_S6_HW_I2C_BUSY_STATE;
		WREG32(RADEON_BIOS_6_SCRATCH, tmp);
	}

	mutex_unlock(&rdev->pm.mutex);
	mutex_unlock(&rdev->dc_hw_i2c_mutex);

	return ret;
}

/* hw i2c engine for r5xx hardware
 * hw can buffer up to 15 bytes
 */
static int r500_hw_i2c_xfer(struct i2c_adapter *i2c_adap,
			    struct i2c_msg *msgs, int num)
{
	struct radeon_i2c_chan *i2c = i2c_get_adapdata(i2c_adap);
	struct radeon_device *rdev = i2c->dev->dev_private;
	struct radeon_i2c_bus_rec *rec = &i2c->rec;
	struct i2c_msg *p;
	int i, j, remaining, current_count, buffer_offset, ret = num;
	u32 prescale;
	u32 tmp, reg;
	u32 saved1, saved2;

	mutex_lock(&rdev->dc_hw_i2c_mutex);
	/* take the pm lock since we need a constant sclk */
	mutex_lock(&rdev->pm.mutex);

	prescale = radeon_get_i2c_prescale(rdev);

	/* clear gpio mask bits */
	tmp = RREG32(rec->mask_clk_reg);
	tmp &= ~rec->mask_clk_mask;
	WREG32(rec->mask_clk_reg, tmp);
	tmp = RREG32(rec->mask_clk_reg);

	tmp = RREG32(rec->mask_data_reg);
	tmp &= ~rec->mask_data_mask;
	WREG32(rec->mask_data_reg, tmp);
	tmp = RREG32(rec->mask_data_reg);

	/* clear pin values */
	tmp = RREG32(rec->a_clk_reg);
	tmp &= ~rec->a_clk_mask;
	WREG32(rec->a_clk_reg, tmp);
	tmp = RREG32(rec->a_clk_reg);

	tmp = RREG32(rec->a_data_reg);
	tmp &= ~rec->a_data_mask;
	WREG32(rec->a_data_reg, tmp);
	tmp = RREG32(rec->a_data_reg);

	/* set the pins to input */
	tmp = RREG32(rec->en_clk_reg);
	tmp &= ~rec->en_clk_mask;
	WREG32(rec->en_clk_reg, tmp);
	tmp = RREG32(rec->en_clk_reg);

	tmp = RREG32(rec->en_data_reg);
	tmp &= ~rec->en_data_mask;
	WREG32(rec->en_data_reg, tmp);
	tmp = RREG32(rec->en_data_reg);

	/* */
	tmp = RREG32(RADEON_BIOS_6_SCRATCH);
	WREG32(RADEON_BIOS_6_SCRATCH, tmp | ATOM_S6_HW_I2C_BUSY_STATE);
	saved1 = RREG32(AVIVO_DC_I2C_CONTROL1);
	saved2 = RREG32(0x494);
	WREG32(0x494, saved2 | 0x1);

	WREG32(AVIVO_DC_I2C_ARBITRATION, AVIVO_DC_I2C_SW_WANTS_TO_USE_I2C);
	for (i = 0; i < 50; i++) {
		udelay(1);
		if (RREG32(AVIVO_DC_I2C_ARBITRATION) & AVIVO_DC_I2C_SW_CAN_USE_I2C)
			break;
	}
	if (i == 50) {
		DRM_ERROR("failed to get i2c bus\n");
		ret = -EBUSY;
		goto done;
	}

	reg = AVIVO_DC_I2C_START | AVIVO_DC_I2C_STOP | AVIVO_DC_I2C_EN;
	switch (rec->mask_clk_reg) {
	case AVIVO_DC_GPIO_DDC1_MASK:
		reg |= AVIVO_DC_I2C_PIN_SELECT(AVIVO_SEL_DDC1);
		break;
	case AVIVO_DC_GPIO_DDC2_MASK:
		reg |= AVIVO_DC_I2C_PIN_SELECT(AVIVO_SEL_DDC2);
		break;
	case AVIVO_DC_GPIO_DDC3_MASK:
		reg |= AVIVO_DC_I2C_PIN_SELECT(AVIVO_SEL_DDC3);
		break;
	default:
		DRM_ERROR("gpio not supported with hw i2c\n");
		ret = -EINVAL;
		goto done;
	}

	/* check for bus probe */
	p = &msgs[0];
	if ((num == 1) && (p->len == 0)) {
		WREG32(AVIVO_DC_I2C_STATUS1, (AVIVO_DC_I2C_DONE |
					      AVIVO_DC_I2C_NACK |
					      AVIVO_DC_I2C_HALT));
		WREG32(AVIVO_DC_I2C_RESET, AVIVO_DC_I2C_SOFT_RESET);
		udelay(1);
		WREG32(AVIVO_DC_I2C_RESET, 0);

		WREG32(AVIVO_DC_I2C_DATA, (p->addr << 1) & 0xff);
		WREG32(AVIVO_DC_I2C_DATA, 0);

		WREG32(AVIVO_DC_I2C_CONTROL3, AVIVO_DC_I2C_TIME_LIMIT(48));
		WREG32(AVIVO_DC_I2C_CONTROL2, (AVIVO_DC_I2C_ADDR_COUNT(1) |
					       AVIVO_DC_I2C_DATA_COUNT(1) |
					       (prescale << 16)));
		WREG32(AVIVO_DC_I2C_CONTROL1, reg);
		WREG32(AVIVO_DC_I2C_STATUS1, AVIVO_DC_I2C_GO);
		for (j = 0; j < 200; j++) {
			udelay(50);
			tmp = RREG32(AVIVO_DC_I2C_STATUS1);
			if (tmp & AVIVO_DC_I2C_GO)
				continue;
			tmp = RREG32(AVIVO_DC_I2C_STATUS1);
			if (tmp & AVIVO_DC_I2C_DONE)
				break;
			else {
				DRM_DEBUG("i2c write error 0x%08x\n", tmp);
				WREG32(AVIVO_DC_I2C_RESET, AVIVO_DC_I2C_ABORT);
				ret = -EIO;
				goto done;
			}
		}
		goto done;
	}

	for (i = 0; i < num; i++) {
		p = &msgs[i];
		remaining = p->len;
		buffer_offset = 0;
		if (p->flags & I2C_M_RD) {
			while (remaining) {
				if (remaining > 15)
					current_count = 15;
				else
					current_count = remaining;
				WREG32(AVIVO_DC_I2C_STATUS1, (AVIVO_DC_I2C_DONE |
							      AVIVO_DC_I2C_NACK |
							      AVIVO_DC_I2C_HALT));
				WREG32(AVIVO_DC_I2C_RESET, AVIVO_DC_I2C_SOFT_RESET);
				udelay(1);
				WREG32(AVIVO_DC_I2C_RESET, 0);

				WREG32(AVIVO_DC_I2C_DATA, ((p->addr << 1) & 0xff) | 0x1);
				WREG32(AVIVO_DC_I2C_CONTROL3, AVIVO_DC_I2C_TIME_LIMIT(48));
				WREG32(AVIVO_DC_I2C_CONTROL2, (AVIVO_DC_I2C_ADDR_COUNT(1) |
							       AVIVO_DC_I2C_DATA_COUNT(current_count) |
							       (prescale << 16)));
				WREG32(AVIVO_DC_I2C_CONTROL1, reg | AVIVO_DC_I2C_RECEIVE);
				WREG32(AVIVO_DC_I2C_STATUS1, AVIVO_DC_I2C_GO);
				for (j = 0; j < 200; j++) {
					udelay(50);
					tmp = RREG32(AVIVO_DC_I2C_STATUS1);
					if (tmp & AVIVO_DC_I2C_GO)
						continue;
					tmp = RREG32(AVIVO_DC_I2C_STATUS1);
					if (tmp & AVIVO_DC_I2C_DONE)
						break;
					else {
						DRM_DEBUG("i2c read error 0x%08x\n", tmp);
						WREG32(AVIVO_DC_I2C_RESET, AVIVO_DC_I2C_ABORT);
						ret = -EIO;
						goto done;
					}
				}
				for (j = 0; j < current_count; j++)
					p->buf[buffer_offset + j] = RREG32(AVIVO_DC_I2C_DATA) & 0xff;
				remaining -= current_count;
				buffer_offset += current_count;
			}
		} else {
			while (remaining) {
				if (remaining > 15)
					current_count = 15;
				else
					current_count = remaining;
				WREG32(AVIVO_DC_I2C_STATUS1, (AVIVO_DC_I2C_DONE |
							      AVIVO_DC_I2C_NACK |
							      AVIVO_DC_I2C_HALT));
				WREG32(AVIVO_DC_I2C_RESET, AVIVO_DC_I2C_SOFT_RESET);
				udelay(1);
				WREG32(AVIVO_DC_I2C_RESET, 0);

				WREG32(AVIVO_DC_I2C_DATA, (p->addr << 1) & 0xff);
				for (j = 0; j < current_count; j++)
					WREG32(AVIVO_DC_I2C_DATA, p->buf[buffer_offset + j]);

				WREG32(AVIVO_DC_I2C_CONTROL3, AVIVO_DC_I2C_TIME_LIMIT(48));
				WREG32(AVIVO_DC_I2C_CONTROL2, (AVIVO_DC_I2C_ADDR_COUNT(1) |
							       AVIVO_DC_I2C_DATA_COUNT(current_count) |
							       (prescale << 16)));
				WREG32(AVIVO_DC_I2C_CONTROL1, reg);
				WREG32(AVIVO_DC_I2C_STATUS1, AVIVO_DC_I2C_GO);
				for (j = 0; j < 200; j++) {
					udelay(50);
					tmp = RREG32(AVIVO_DC_I2C_STATUS1);
					if (tmp & AVIVO_DC_I2C_GO)
						continue;
					tmp = RREG32(AVIVO_DC_I2C_STATUS1);
					if (tmp & AVIVO_DC_I2C_DONE)
						break;
					else {
						DRM_DEBUG("i2c write error 0x%08x\n", tmp);
						WREG32(AVIVO_DC_I2C_RESET, AVIVO_DC_I2C_ABORT);
						ret = -EIO;
						goto done;
					}
				}
				remaining -= current_count;
				buffer_offset += current_count;
			}
		}
	}

done:
	WREG32(AVIVO_DC_I2C_STATUS1, (AVIVO_DC_I2C_DONE |
				      AVIVO_DC_I2C_NACK |
				      AVIVO_DC_I2C_HALT));
	WREG32(AVIVO_DC_I2C_RESET, AVIVO_DC_I2C_SOFT_RESET);
	udelay(1);
	WREG32(AVIVO_DC_I2C_RESET, 0);

	WREG32(AVIVO_DC_I2C_ARBITRATION, AVIVO_DC_I2C_SW_DONE_USING_I2C);
	WREG32(AVIVO_DC_I2C_CONTROL1, saved1);
	WREG32(0x494, saved2);
	tmp = RREG32(RADEON_BIOS_6_SCRATCH);
	tmp &= ~ATOM_S6_HW_I2C_BUSY_STATE;
	WREG32(RADEON_BIOS_6_SCRATCH, tmp);

	mutex_unlock(&rdev->pm.mutex);
	mutex_unlock(&rdev->dc_hw_i2c_mutex);

	return ret;
}

static int radeon_hw_i2c_xfer(struct i2c_adapter *i2c_adap,
			      struct i2c_msg *msgs, int num)
{
	struct radeon_i2c_chan *i2c = i2c_get_adapdata(i2c_adap);
	struct radeon_device *rdev = i2c->dev->dev_private;
	struct radeon_i2c_bus_rec *rec = &i2c->rec;
	int ret = 0;

	switch (rdev->family) {
	case CHIP_R100:
	case CHIP_RV100:
	case CHIP_RS100:
	case CHIP_RV200:
	case CHIP_RS200:
	case CHIP_R200:
	case CHIP_RV250:
	case CHIP_RS300:
	case CHIP_RV280:
	case CHIP_R300:
	case CHIP_R350:
	case CHIP_RV350:
	case CHIP_RV380:
	case CHIP_R420:
	case CHIP_R423:
	case CHIP_RV410:
	case CHIP_RS400:
	case CHIP_RS480:
		ret = r100_hw_i2c_xfer(i2c_adap, msgs, num);
		break;
	case CHIP_RS600:
	case CHIP_RS690:
	case CHIP_RS740:
		/* XXX fill in hw i2c implementation */
		break;
	case CHIP_RV515:
	case CHIP_R520:
	case CHIP_RV530:
	case CHIP_RV560:
	case CHIP_RV570:
	case CHIP_R580:
		if (rec->mm_i2c)
			ret = r100_hw_i2c_xfer(i2c_adap, msgs, num);
		else
			ret = r500_hw_i2c_xfer(i2c_adap, msgs, num);
		break;
	case CHIP_R600:
	case CHIP_RV610:
	case CHIP_RV630:
	case CHIP_RV670:
		/* XXX fill in hw i2c implementation */
		break;
	case CHIP_RV620:
	case CHIP_RV635:
	case CHIP_RS780:
	case CHIP_RS880:
	case CHIP_RV770:
	case CHIP_RV730:
	case CHIP_RV710:
	case CHIP_RV740:
		/* XXX fill in hw i2c implementation */
		break;
	case CHIP_CEDAR:
	case CHIP_REDWOOD:
	case CHIP_JUNIPER:
	case CHIP_CYPRESS:
	case CHIP_HEMLOCK:
		/* XXX fill in hw i2c implementation */
		break;
	default:
		DRM_ERROR("i2c: unhandled radeon chip\n");
		ret = -EIO;
		break;
	}

	return ret;
}

static u32 radeon_hw_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm radeon_i2c_algo = {
	.master_xfer = radeon_hw_i2c_xfer,
	.functionality = radeon_hw_i2c_func,
};

struct radeon_i2c_chan *radeon_i2c_create(struct drm_device *dev,
					  struct radeon_i2c_bus_rec *rec,
					  const char *name)
{
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_i2c_chan *i2c;
	int ret;

	i2c = kzalloc(sizeof(struct radeon_i2c_chan), GFP_KERNEL);
	if (i2c == NULL)
		return NULL;

	i2c->rec = *rec;
	i2c->adapter.owner = THIS_MODULE;
	i2c->dev = dev;
	i2c_set_adapdata(&i2c->adapter, i2c);
	if (rec->mm_i2c ||
	    (rec->hw_capable &&
	     radeon_hw_i2c &&
	     ((rdev->family <= CHIP_RS480) ||
	      ((rdev->family >= CHIP_RV515) && (rdev->family <= CHIP_R580))))) {
		/* set the radeon hw i2c adapter */
		sprintf(i2c->adapter.name, "Radeon i2c hw bus %s", name);
		i2c->adapter.algo = &radeon_i2c_algo;
		ret = i2c_add_adapter(&i2c->adapter);
		if (ret) {
			DRM_ERROR("Failed to register hw i2c %s\n", name);
			goto out_free;
		}
	} else {
		/* set the radeon bit adapter */
		sprintf(i2c->adapter.name, "Radeon i2c bit bus %s", name);
		i2c->adapter.algo_data = &i2c->algo.bit;
		i2c->algo.bit.pre_xfer = pre_xfer;
		i2c->algo.bit.post_xfer = post_xfer;
		i2c->algo.bit.setsda = set_data;
		i2c->algo.bit.setscl = set_clock;
		i2c->algo.bit.getsda = get_data;
		i2c->algo.bit.getscl = get_clock;
		i2c->algo.bit.udelay = 20;
		/* vesa says 2.2 ms is enough, 1 jiffy doesn't seem to always
		 * make this, 2 jiffies is a lot more reliable */
		i2c->algo.bit.timeout = 2;
		i2c->algo.bit.data = i2c;
		ret = i2c_bit_add_bus(&i2c->adapter);
		if (ret) {
			DRM_ERROR("Failed to register bit i2c %s\n", name);
			goto out_free;
		}
	}

	return i2c;
out_free:
	kfree(i2c);
	return NULL;

}

struct radeon_i2c_chan *radeon_i2c_create_dp(struct drm_device *dev,
					     struct radeon_i2c_bus_rec *rec,
					     const char *name)
{
	struct radeon_i2c_chan *i2c;
	int ret;

	i2c = kzalloc(sizeof(struct radeon_i2c_chan), GFP_KERNEL);
	if (i2c == NULL)
		return NULL;

	i2c->rec = *rec;
	i2c->adapter.owner = THIS_MODULE;
	i2c->dev = dev;
	i2c_set_adapdata(&i2c->adapter, i2c);
	i2c->adapter.algo_data = &i2c->algo.dp;
	i2c->algo.dp.aux_ch = radeon_dp_i2c_aux_ch;
	i2c->algo.dp.address = 0;
	ret = i2c_dp_aux_add_bus(&i2c->adapter);
	if (ret) {
		DRM_INFO("Failed to register i2c %s\n", name);
		goto out_free;
	}

	return i2c;
out_free:
	kfree(i2c);
	return NULL;

}

void radeon_i2c_destroy(struct radeon_i2c_chan *i2c)
{
	if (!i2c)
		return;
	i2c_del_adapter(&i2c->adapter);
	kfree(i2c);
}

struct drm_encoder *radeon_best_encoder(struct drm_connector *connector)
{
	return NULL;
}

void radeon_i2c_get_byte(struct radeon_i2c_chan *i2c_bus,
			 u8 slave_addr,
			 u8 addr,
			 u8 *val)
{
	u8 out_buf[2];
	u8 in_buf[2];
	struct i2c_msg msgs[] = {
		{
			.addr = slave_addr,
			.flags = 0,
			.len = 1,
			.buf = out_buf,
		},
		{
			.addr = slave_addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = in_buf,
		}
	};

	out_buf[0] = addr;
	out_buf[1] = 0;

	if (i2c_transfer(&i2c_bus->adapter, msgs, 2) == 2) {
		*val = in_buf[0];
		DRM_DEBUG("val = 0x%02x\n", *val);
	} else {
		DRM_ERROR("i2c 0x%02x 0x%02x read failed\n",
			  addr, *val);
	}
}

void radeon_i2c_put_byte(struct radeon_i2c_chan *i2c_bus,
			 u8 slave_addr,
			 u8 addr,
			 u8 val)
{
	uint8_t out_buf[2];
	struct i2c_msg msg = {
		.addr = slave_addr,
		.flags = 0,
		.len = 2,
		.buf = out_buf,
	};

	out_buf[0] = addr;
	out_buf[1] = val;

	if (i2c_transfer(&i2c_bus->adapter, &msg, 1) != 1)
		DRM_ERROR("i2c 0x%02x 0x%02x write failed\n",
			  addr, val);
}


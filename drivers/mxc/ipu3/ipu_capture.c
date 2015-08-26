/*
 * Copyright 2008-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @file ipu_capture.c
 *
 * @brief IPU capture dase functions
 *
 * @ingroup IPU
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ipu-v3.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "ipu_prv.h"
#include "ipu_regs.h"

/*!
 * _ipu_csi_mclk_set
 *
 * @param	ipu		ipu handler
 * @param	pixel_clk   desired pixel clock frequency in Hz
 * @param	csi         csi 0 or csi 1
 *
 * @return	Returns 0 on success or negative error code on fail
 */
int _ipu_csi_mclk_set(struct ipu_soc *ipu, uint32_t pixel_clk, uint32_t csi)
{
	uint32_t temp;
	uint32_t div_ratio;

	div_ratio = (clk_get_rate(ipu->ipu_clk) / pixel_clk) - 1;

	if (div_ratio > 0xFF || div_ratio < 0) {
		dev_dbg(ipu->dev, "value of pixel_clk extends normal range\n");
		return -EINVAL;
	}

	temp = ipu_csi_read(ipu, csi, CSI_SENS_CONF);
	temp &= ~CSI_SENS_CONF_DIVRATIO_MASK;
	ipu_csi_write(ipu, csi, temp |
			(div_ratio << CSI_SENS_CONF_DIVRATIO_SHIFT),
			CSI_SENS_CONF);

	return 0;
}

/*!
 * ipu_csi_init_interface
 *	Sets initial values for the CSI registers.
 *	The width and height of the sensor and the actual frame size will be
 *	set to the same values.
 * @param	ipu		ipu handler
 * @param	width		Sensor width
 * @param       height		Sensor height
 * @param       pixel_fmt	pixel format
 * @param       cfg_param	ipu_csi_signal_cfg_t structure
 * @param       csi             csi 0 or csi 1
 *
 * @return      0 for success, -EINVAL for error
 */
int32_t
ipu_csi_init_interface(struct ipu_soc *ipu, uint16_t width, uint16_t height,
	uint32_t pixel_fmt, ipu_csi_signal_cfg_t cfg_param)
{
	uint32_t data = 0;
	uint32_t csi = cfg_param.csi;

	/* Set SENS_DATA_FORMAT bits (8, 9 and 10)
	   RGB or YUV444 is 0 which is current value in data so not set
	   explicitly
	   This is also the default value if attempts are made to set it to
	   something invalid. */
	switch (pixel_fmt) {
	case IPU_PIX_FMT_YUYV:
		cfg_param.data_fmt = CSI_SENS_CONF_DATA_FMT_YUV422_YUYV;
		break;
	case IPU_PIX_FMT_UYVY:
		cfg_param.data_fmt = CSI_SENS_CONF_DATA_FMT_YUV422_UYVY;
		break;
	case IPU_PIX_FMT_RGB24:
	case IPU_PIX_FMT_BGR24:
		cfg_param.data_fmt = CSI_SENS_CONF_DATA_FMT_RGB_YUV444;
		break;
	case IPU_PIX_FMT_GENERIC:
	case IPU_PIX_FMT_GENERIC_16:
		cfg_param.data_fmt = CSI_SENS_CONF_DATA_FMT_BAYER;
		break;
	case IPU_PIX_FMT_RGB565:
		cfg_param.data_fmt = CSI_SENS_CONF_DATA_FMT_RGB565;
		break;
	case IPU_PIX_FMT_RGB555:
		cfg_param.data_fmt = CSI_SENS_CONF_DATA_FMT_RGB555;
		break;
	default:
		return -EINVAL;
	}

	/* Set the CSI_SENS_CONF register remaining fields */
	data |= cfg_param.data_width << CSI_SENS_CONF_DATA_WIDTH_SHIFT |
		cfg_param.data_fmt << CSI_SENS_CONF_DATA_FMT_SHIFT |
		cfg_param.data_pol << CSI_SENS_CONF_DATA_POL_SHIFT |
		cfg_param.Vsync_pol << CSI_SENS_CONF_VSYNC_POL_SHIFT |
		cfg_param.Hsync_pol << CSI_SENS_CONF_HSYNC_POL_SHIFT |
		cfg_param.pixclk_pol << CSI_SENS_CONF_PIX_CLK_POL_SHIFT |
		cfg_param.ext_vsync << CSI_SENS_CONF_EXT_VSYNC_SHIFT |
		cfg_param.clk_mode << CSI_SENS_CONF_SENS_PRTCL_SHIFT |
		cfg_param.pack_tight << CSI_SENS_CONF_PACK_TIGHT_SHIFT |
		cfg_param.force_eof << CSI_SENS_CONF_FORCE_EOF_SHIFT |
		cfg_param.data_en_pol << CSI_SENS_CONF_DATA_EN_POL_SHIFT;

	_ipu_get(ipu);

	mutex_lock(&ipu->mutex_lock);

	ipu_csi_write(ipu, csi, data, CSI_SENS_CONF);

	/* Setup sensor frame size */
	ipu_csi_write(ipu, csi, (width - 1) | (height - 1) << 16, CSI_SENS_FRM_SIZE);

	/* Set CCIR registers */
	if (cfg_param.clk_mode == IPU_CSI_CLK_MODE_CCIR656_PROGRESSIVE) {
		ipu_csi_write(ipu, csi, 0x40030, CSI_CCIR_CODE_1);
		ipu_csi_write(ipu, csi, 0xFF0000, CSI_CCIR_CODE_3);
	} else if (cfg_param.clk_mode == IPU_CSI_CLK_MODE_CCIR656_INTERLACED) {
		if (width == 720 && height == 625) {
			/* PAL case */
			/*
			 * Field0BlankEnd = 0x6, Field0BlankStart = 0x2,
			 * Field0ActiveEnd = 0x4, Field0ActiveStart = 0
			 */
			ipu_csi_write(ipu, csi, 0x40596, CSI_CCIR_CODE_1);
			/*
			 * Field1BlankEnd = 0x7, Field1BlankStart = 0x3,
			 * Field1ActiveEnd = 0x5, Field1ActiveStart = 0x1
			 */
			ipu_csi_write(ipu, csi, 0xD07DF, CSI_CCIR_CODE_2);

			ipu_csi_write(ipu, csi, 0xFF0000, CSI_CCIR_CODE_3);

		} else if (width == 720 && height == 525) {
			/* NTSC case */
			/*
			 * Field0BlankEnd = 0x7, Field0BlankStart = 0x3,
			 * Field0ActiveEnd = 0x5, Field0ActiveStart = 0x1
			 */
			ipu_csi_write(ipu, csi, 0xD07DF, CSI_CCIR_CODE_1);
			/*
			 * Field1BlankEnd = 0x6, Field1BlankStart = 0x2,
			 * Field1ActiveEnd = 0x4, Field1ActiveStart = 0
			 */
			ipu_csi_write(ipu, csi, 0x40596, CSI_CCIR_CODE_2);
			ipu_csi_write(ipu, csi, 0xFF0000, CSI_CCIR_CODE_3);
		} else {
			dev_err(ipu->dev, "Unsupported CCIR656 interlaced "
					"video mode\n");
			mutex_unlock(&ipu->mutex_lock);
			_ipu_put(ipu);
			return -EINVAL;
		}
		_ipu_csi_ccir_err_detection_enable(ipu, csi);
	} else if ((cfg_param.clk_mode ==
			IPU_CSI_CLK_MODE_CCIR1120_PROGRESSIVE_DDR) ||
		(cfg_param.clk_mode ==
			IPU_CSI_CLK_MODE_CCIR1120_PROGRESSIVE_SDR) ||
		(cfg_param.clk_mode ==
			IPU_CSI_CLK_MODE_CCIR1120_INTERLACED_DDR) ||
		(cfg_param.clk_mode ==
			IPU_CSI_CLK_MODE_CCIR1120_INTERLACED_SDR)) {
		ipu_csi_write(ipu, csi, 0x40030, CSI_CCIR_CODE_1);
		ipu_csi_write(ipu, csi, 0xFF0000, CSI_CCIR_CODE_3);
		_ipu_csi_ccir_err_detection_enable(ipu, csi);
	} else if ((cfg_param.clk_mode == IPU_CSI_CLK_MODE_GATED_CLK) ||
		   (cfg_param.clk_mode == IPU_CSI_CLK_MODE_NONGATED_CLK)) {
		_ipu_csi_ccir_err_detection_disable(ipu, csi);
	}

	dev_dbg(ipu->dev, "CSI_SENS_CONF = 0x%08X\n",
		ipu_csi_read(ipu, csi, CSI_SENS_CONF));
	dev_dbg(ipu->dev, "CSI_ACT_FRM_SIZE = 0x%08X\n",
		ipu_csi_read(ipu, csi, CSI_ACT_FRM_SIZE));

	mutex_unlock(&ipu->mutex_lock);

	_ipu_put(ipu);

	return 0;
}
EXPORT_SYMBOL(ipu_csi_init_interface);

/*!
 * ipu_csi_get_sensor_protocol
 *
 * @param	ipu		ipu handler
 * @param	csi         csi 0 or csi 1
 *
 * @return	Returns sensor protocol
 */
int32_t ipu_csi_get_sensor_protocol(struct ipu_soc *ipu, uint32_t csi)
{
	int ret;
	_ipu_get(ipu);
	ret = (ipu_csi_read(ipu, csi, CSI_SENS_CONF) &
		CSI_SENS_CONF_SENS_PRTCL_MASK) >>
		CSI_SENS_CONF_SENS_PRTCL_SHIFT;
	_ipu_put(ipu);
	return ret;
}
EXPORT_SYMBOL(ipu_csi_get_sensor_protocol);

/*!
 * ipu_csi_enable_mclk
 *
 * @param	ipu		ipu handler
 * @param	csi         csi 0 or csi 1
 * @param       flag        true to enable mclk, false to disable mclk
 * @param       wait        true to wait 100ms make clock stable, false not wait
 *
 * @return      Returns 0 on success
 */
int ipu_csi_enable_mclk(struct ipu_soc *ipu, int csi, bool flag, bool wait)
{
	/* Return immediately if there is no csi_clk to manage */
	if (ipu->csi_clk[csi] == NULL)
		return 0;

	if (flag) {
		clk_enable(ipu->csi_clk[csi]);
		if (wait == true)
			msleep(10);
	} else {
		clk_disable(ipu->csi_clk[csi]);
	}

	return 0;
}
EXPORT_SYMBOL(ipu_csi_enable_mclk);

/*!
 * ipu_csi_get_window_size
 *
 * @param	ipu		ipu handler
 * @param	width	pointer to window width
 * @param	height	pointer to window height
 * @param	csi	csi 0 or csi 1
 */
void ipu_csi_get_window_size(struct ipu_soc *ipu, uint32_t *width, uint32_t *height, uint32_t csi)
{
	uint32_t reg;

	_ipu_get(ipu);

	mutex_lock(&ipu->mutex_lock);

	reg = ipu_csi_read(ipu, csi, CSI_ACT_FRM_SIZE);
	*width = (reg & 0xFFFF) + 1;
	*height = (reg >> 16 & 0xFFFF) + 1;

	mutex_unlock(&ipu->mutex_lock);

	_ipu_put(ipu);
}
EXPORT_SYMBOL(ipu_csi_get_window_size);

/*!
 * ipu_csi_set_window_size
 *
 * @param	ipu		ipu handler
 * @param	width	window width
 * @param       height	window height
 * @param       csi	csi 0 or csi 1
 */
void ipu_csi_set_window_size(struct ipu_soc *ipu, uint32_t width, uint32_t height, uint32_t csi)
{
	_ipu_get(ipu);

	mutex_lock(&ipu->mutex_lock);

	ipu_csi_write(ipu, csi, (width - 1) | (height - 1) << 16, CSI_ACT_FRM_SIZE);

	mutex_unlock(&ipu->mutex_lock);

	_ipu_put(ipu);
}
EXPORT_SYMBOL(ipu_csi_set_window_size);

/*!
 * ipu_csi_set_window_pos
 *
 * @param	ipu		ipu handler
 * @param       left	uint32 window x start
 * @param       top	uint32 window y start
 * @param       csi	csi 0 or csi 1
 */
void ipu_csi_set_window_pos(struct ipu_soc *ipu, uint32_t left, uint32_t top, uint32_t csi)
{
	uint32_t temp;

	_ipu_get(ipu);

	mutex_lock(&ipu->mutex_lock);

	temp = ipu_csi_read(ipu, csi, CSI_OUT_FRM_CTRL);
	temp &= ~(CSI_HSC_MASK | CSI_VSC_MASK);
	temp |= ((top << CSI_VSC_SHIFT) | (left << CSI_HSC_SHIFT));
	ipu_csi_write(ipu, csi, temp, CSI_OUT_FRM_CTRL);

	mutex_unlock(&ipu->mutex_lock);

	_ipu_put(ipu);
}
EXPORT_SYMBOL(ipu_csi_set_window_pos);

/*!
 * _ipu_csi_horizontal_downsize_enable
 *	Enable horizontal downsizing(decimation) by 2.
 *
 * @param	ipu		ipu handler
 * @param	csi	csi 0 or csi 1
 */
void _ipu_csi_horizontal_downsize_enable(struct ipu_soc *ipu, uint32_t csi)
{
	uint32_t temp;

	temp = ipu_csi_read(ipu, csi, CSI_OUT_FRM_CTRL);
	temp |= CSI_HORI_DOWNSIZE_EN;
	ipu_csi_write(ipu, csi, temp, CSI_OUT_FRM_CTRL);
}

/*!
 * _ipu_csi_horizontal_downsize_disable
 *	Disable horizontal downsizing(decimation) by 2.
 *
 * @param	ipu		ipu handler
 * @param	csi	csi 0 or csi 1
 */
void _ipu_csi_horizontal_downsize_disable(struct ipu_soc *ipu, uint32_t csi)
{
	uint32_t temp;

	temp = ipu_csi_read(ipu, csi, CSI_OUT_FRM_CTRL);
	temp &= ~CSI_HORI_DOWNSIZE_EN;
	ipu_csi_write(ipu, csi, temp, CSI_OUT_FRM_CTRL);
}

/*!
 * _ipu_csi_vertical_downsize_enable
 *	Enable vertical downsizing(decimation) by 2.
 *
 * @param	ipu		ipu handler
 * @param	csi	csi 0 or csi 1
 */
void _ipu_csi_vertical_downsize_enable(struct ipu_soc *ipu, uint32_t csi)
{
	uint32_t temp;

	temp = ipu_csi_read(ipu, csi, CSI_OUT_FRM_CTRL);
	temp |= CSI_VERT_DOWNSIZE_EN;
	ipu_csi_write(ipu, csi, temp, CSI_OUT_FRM_CTRL);
}

/*!
 * _ipu_csi_vertical_downsize_disable
 *	Disable vertical downsizing(decimation) by 2.
 *
 * @param	ipu		ipu handler
 * @param	csi	csi 0 or csi 1
 */
void _ipu_csi_vertical_downsize_disable(struct ipu_soc *ipu, uint32_t csi)
{
	uint32_t temp;

	temp = ipu_csi_read(ipu, csi, CSI_OUT_FRM_CTRL);
	temp &= ~CSI_VERT_DOWNSIZE_EN;
	ipu_csi_write(ipu, csi, temp, CSI_OUT_FRM_CTRL);
}

/*!
 * _ipu_csi_set_test_generator
 *
 * @param	ipu		ipu handler
 * @param	active       1 for active and 0 for inactive
 * @param       r_value	     red value for the generated pattern of even pixel
 * @param       g_value      green value for the generated pattern of even
 *			     pixel
 * @param       b_value      blue value for the generated pattern of even pixel
 * @param	pixel_clk   desired pixel clock frequency in Hz
 * @param       csi          csi 0 or csi 1
 */
void _ipu_csi_set_test_generator(struct ipu_soc *ipu, bool active, uint32_t r_value,
	uint32_t g_value, uint32_t b_value, uint32_t pix_clk, uint32_t csi)
{
	uint32_t temp;

	temp = ipu_csi_read(ipu, csi, CSI_TST_CTRL);

	if (active == false) {
		temp &= ~CSI_TEST_GEN_MODE_EN;
		ipu_csi_write(ipu, csi, temp, CSI_TST_CTRL);
	} else {
		/* Set sensb_mclk div_ratio*/
		_ipu_csi_mclk_set(ipu, pix_clk, csi);

		temp &= ~(CSI_TEST_GEN_R_MASK | CSI_TEST_GEN_G_MASK |
			CSI_TEST_GEN_B_MASK);
		temp |= CSI_TEST_GEN_MODE_EN;
		temp |= (r_value << CSI_TEST_GEN_R_SHIFT) |
			(g_value << CSI_TEST_GEN_G_SHIFT) |
			(b_value << CSI_TEST_GEN_B_SHIFT);
		ipu_csi_write(ipu, csi, temp, CSI_TST_CTRL);
	}
}

/*!
 * _ipu_csi_ccir_err_detection_en
 *	Enable error detection and correction for
 *	CCIR interlaced mode with protection bit.
 *
 * @param	ipu		ipu handler
 * @param	csi	csi 0 or csi 1
 */
void _ipu_csi_ccir_err_detection_enable(struct ipu_soc *ipu, uint32_t csi)
{
	uint32_t temp;

	temp = ipu_csi_read(ipu, csi, CSI_CCIR_CODE_1);
	temp |= CSI_CCIR_ERR_DET_EN;
	ipu_csi_write(ipu, csi, temp, CSI_CCIR_CODE_1);

}

/*!
 * _ipu_csi_ccir_err_detection_disable
 *	Disable error detection and correction for
 *	CCIR interlaced mode with protection bit.
 *
 * @param	ipu		ipu handler
 * @param	csi	csi 0 or csi 1
 */
void _ipu_csi_ccir_err_detection_disable(struct ipu_soc *ipu, uint32_t csi)
{
	uint32_t temp;

	temp = ipu_csi_read(ipu, csi, CSI_CCIR_CODE_1);
	temp &= ~CSI_CCIR_ERR_DET_EN;
	ipu_csi_write(ipu, csi, temp, CSI_CCIR_CODE_1);

}

/*!
 * _ipu_csi_set_mipi_di
 *
 * @param	ipu		ipu handler
 * @param	num	MIPI data identifier 0-3 handled by CSI
 * @param	di_val	data identifier value
 * @param	csi	csi 0 or csi 1
 *
 * @return	Returns 0 on success or negative error code on fail
 */
int _ipu_csi_set_mipi_di(struct ipu_soc *ipu, uint32_t num, uint32_t di_val, uint32_t csi)
{
	uint32_t temp;
	int retval = 0;

	if (di_val > 0xFFL) {
		retval = -EINVAL;
		goto err;
	}

	temp = ipu_csi_read(ipu, csi, CSI_MIPI_DI);

	switch (num) {
	case IPU_CSI_MIPI_DI0:
		temp &= ~CSI_MIPI_DI0_MASK;
		temp |= (di_val << CSI_MIPI_DI0_SHIFT);
		ipu_csi_write(ipu, csi, temp, CSI_MIPI_DI);
		break;
	case IPU_CSI_MIPI_DI1:
		temp &= ~CSI_MIPI_DI1_MASK;
		temp |= (di_val << CSI_MIPI_DI1_SHIFT);
		ipu_csi_write(ipu, csi, temp, CSI_MIPI_DI);
		break;
	case IPU_CSI_MIPI_DI2:
		temp &= ~CSI_MIPI_DI2_MASK;
		temp |= (di_val << CSI_MIPI_DI2_SHIFT);
		ipu_csi_write(ipu, csi, temp, CSI_MIPI_DI);
		break;
	case IPU_CSI_MIPI_DI3:
		temp &= ~CSI_MIPI_DI3_MASK;
		temp |= (di_val << CSI_MIPI_DI3_SHIFT);
		ipu_csi_write(ipu, csi, temp, CSI_MIPI_DI);
		break;
	default:
		retval = -EINVAL;
	}

err:
	return retval;
}

/*!
 * _ipu_csi_set_skip_isp
 *
 * @param	ipu		ipu handler
 * @param	skip		select frames to be skipped and set the
 *				correspond bits to 1
 * @param	max_ratio	number of frames in a skipping set and the
 * 				maximum value of max_ratio is 5
 * @param	csi		csi 0 or csi 1
 *
 * @return	Returns 0 on success or negative error code on fail
 */
int _ipu_csi_set_skip_isp(struct ipu_soc *ipu, uint32_t skip, uint32_t max_ratio, uint32_t csi)
{
	uint32_t temp;
	int retval = 0;

	if (max_ratio > 5) {
		retval = -EINVAL;
		goto err;
	}

	temp = ipu_csi_read(ipu, csi, CSI_SKIP);
	temp &= ~(CSI_MAX_RATIO_SKIP_ISP_MASK | CSI_SKIP_ISP_MASK);
	temp |= (max_ratio << CSI_MAX_RATIO_SKIP_ISP_SHIFT) |
		(skip << CSI_SKIP_ISP_SHIFT);
	ipu_csi_write(ipu, csi, temp, CSI_SKIP);

err:
	return retval;
}

/*!
 * _ipu_csi_set_skip_smfc
 *
 * @param	ipu		ipu handler
 * @param	skip		select frames to be skipped and set the
 *				correspond bits to 1
 * @param	max_ratio	number of frames in a skipping set and the
 *				maximum value of max_ratio is 5
 * @param	id		csi to smfc skipping id
 * @param	csi		csi 0 or csi 1
 *
 * @return	Returns 0 on success or negative error code on fail
 */
int _ipu_csi_set_skip_smfc(struct ipu_soc *ipu, uint32_t skip,
	uint32_t max_ratio, uint32_t id, uint32_t csi)
{
	uint32_t temp;
	int retval = 0;

	if (max_ratio > 5 || id > 3) {
		retval = -EINVAL;
		goto err;
	}

	temp = ipu_csi_read(ipu, csi, CSI_SKIP);
	temp &= ~(CSI_MAX_RATIO_SKIP_SMFC_MASK | CSI_ID_2_SKIP_MASK |
			CSI_SKIP_SMFC_MASK);
	temp |= (max_ratio << CSI_MAX_RATIO_SKIP_SMFC_SHIFT) |
			(id << CSI_ID_2_SKIP_SHIFT) |
			(skip << CSI_SKIP_SMFC_SHIFT);
	ipu_csi_write(ipu, csi, temp, CSI_SKIP);

err:
	return retval;
}

/*!
 * _ipu_smfc_init
 *	Map CSI frames to IDMAC channels.
 *
 * @param	ipu		ipu handler
 * @param	channel		IDMAC channel 0-3
 * @param	mipi_id		mipi id number 0-3
 * @param	csi		csi0 or csi1
 */
void _ipu_smfc_init(struct ipu_soc *ipu, ipu_channel_t channel, uint32_t mipi_id, uint32_t csi)
{
	uint32_t temp;

	temp = ipu_smfc_read(ipu, SMFC_MAP);

	switch (channel) {
	case CSI_MEM0:
		temp &= ~SMFC_MAP_CH0_MASK;
		temp |= ((csi << 2) | mipi_id) << SMFC_MAP_CH0_SHIFT;
		break;
	case CSI_MEM1:
		temp &= ~SMFC_MAP_CH1_MASK;
		temp |= ((csi << 2) | mipi_id) << SMFC_MAP_CH1_SHIFT;
		break;
	case CSI_MEM2:
		temp &= ~SMFC_MAP_CH2_MASK;
		temp |= ((csi << 2) | mipi_id) << SMFC_MAP_CH2_SHIFT;
		break;
	case CSI_MEM3:
		temp &= ~SMFC_MAP_CH3_MASK;
		temp |= ((csi << 2) | mipi_id) << SMFC_MAP_CH3_SHIFT;
		break;
	default:
		return;
	}

	ipu_smfc_write(ipu, temp, SMFC_MAP);
}

/*!
 * _ipu_smfc_set_wmc
 *	Caution: The number of required channels,  the enabled channels
 *	and the FIFO size per channel are configured restrictedly.
 *
 * @param	ipu		ipu handler
 * @param	channel		IDMAC channel 0-3
 * @param	set		set 1 or clear 0
 * @param	level		water mark level when FIFO is on the
 *				relative size
 */
void _ipu_smfc_set_wmc(struct ipu_soc *ipu, ipu_channel_t channel, bool set, uint32_t level)
{
	uint32_t temp;

	temp = ipu_smfc_read(ipu, SMFC_WMC);

	switch (channel) {
	case CSI_MEM0:
		if (set == true) {
			temp &= ~SMFC_WM0_SET_MASK;
			temp |= level << SMFC_WM0_SET_SHIFT;
		} else {
			temp &= ~SMFC_WM0_CLR_MASK;
			temp |= level << SMFC_WM0_CLR_SHIFT;
		}
		break;
	case CSI_MEM1:
		if (set == true) {
			temp &= ~SMFC_WM1_SET_MASK;
			temp |= level << SMFC_WM1_SET_SHIFT;
		} else {
			temp &= ~SMFC_WM1_CLR_MASK;
			temp |= level << SMFC_WM1_CLR_SHIFT;
		}
		break;
	case CSI_MEM2:
		if (set == true) {
			temp &= ~SMFC_WM2_SET_MASK;
			temp |= level << SMFC_WM2_SET_SHIFT;
		} else {
			temp &= ~SMFC_WM2_CLR_MASK;
			temp |= level << SMFC_WM2_CLR_SHIFT;
		}
		break;
	case CSI_MEM3:
		if (set == true) {
			temp &= ~SMFC_WM3_SET_MASK;
			temp |= level << SMFC_WM3_SET_SHIFT;
		} else {
			temp &= ~SMFC_WM3_CLR_MASK;
			temp |= level << SMFC_WM3_CLR_SHIFT;
		}
		break;
	default:
		return;
	}

	ipu_smfc_write(ipu, temp, SMFC_WMC);
}

/*!
 * _ipu_smfc_set_burst_size
 *
 * @param	ipu		ipu handler
 * @param	channel		IDMAC channel 0-3
 * @param	bs		burst size of IDMAC channel,
 *				the value programmed here shoud be BURST_SIZE-1
 */
void _ipu_smfc_set_burst_size(struct ipu_soc *ipu, ipu_channel_t channel, uint32_t bs)
{
	uint32_t temp;

	temp = ipu_smfc_read(ipu, SMFC_BS);

	switch (channel) {
	case CSI_MEM0:
		temp &= ~SMFC_BS0_MASK;
		temp |= bs << SMFC_BS0_SHIFT;
		break;
	case CSI_MEM1:
		temp &= ~SMFC_BS1_MASK;
		temp |= bs << SMFC_BS1_SHIFT;
		break;
	case CSI_MEM2:
		temp &= ~SMFC_BS2_MASK;
		temp |= bs << SMFC_BS2_SHIFT;
		break;
	case CSI_MEM3:
		temp &= ~SMFC_BS3_MASK;
		temp |= bs << SMFC_BS3_SHIFT;
		break;
	default:
		return;
	}

	ipu_smfc_write(ipu, temp, SMFC_BS);
}

/*!
 * _ipu_csi_init
 *
 * @param	ipu		ipu handler
 * @param	channel      IDMAC channel
 * @param	csi	     csi 0 or csi 1
 *
 * @return	Returns 0 on success or negative error code on fail
 */
int _ipu_csi_init(struct ipu_soc *ipu, ipu_channel_t channel, uint32_t csi)
{
	uint32_t csi_sens_conf, csi_dest;
	int retval = 0;

	switch (channel) {
	case CSI_MEM0:
	case CSI_MEM1:
	case CSI_MEM2:
	case CSI_MEM3:
		csi_dest = CSI_DATA_DEST_IDMAC;
		break;
	case CSI_PRP_ENC_MEM:
	case CSI_PRP_VF_MEM:
		csi_dest = CSI_DATA_DEST_IC;
		break;
	default:
		retval = -EINVAL;
		goto err;
	}

	csi_sens_conf = ipu_csi_read(ipu, csi, CSI_SENS_CONF);
	csi_sens_conf &= ~CSI_SENS_CONF_DATA_DEST_MASK;
	ipu_csi_write(ipu, csi, csi_sens_conf | (csi_dest <<
		CSI_SENS_CONF_DATA_DEST_SHIFT), CSI_SENS_CONF);
err:
	return retval;
}

/*!
 * csi_irq_handler
 *
 * @param	irq		interrupt id
 * @param	dev_id		pointer to ipu handler
 *
 * @return	Returns if irq is handled
 */
static irqreturn_t csi_irq_handler(int irq, void *dev_id)
{
	struct ipu_soc *ipu = dev_id;
	struct completion *comp = &ipu->csi_comp;

	complete(comp);
	return IRQ_HANDLED;
}

/*!
 * _ipu_csi_wait4eof
 *
 * @param	ipu		ipu handler
 * @param	channel      IDMAC channel
 *
 */
void _ipu_csi_wait4eof(struct ipu_soc *ipu, ipu_channel_t channel)
{
	int ret;
	int irq = 0;

	if (channel == CSI_MEM0)
		irq = IPU_IRQ_CSI0_OUT_EOF;
	else if (channel == CSI_MEM1)
		irq = IPU_IRQ_CSI1_OUT_EOF;
	else if (channel == CSI_MEM2)
		irq = IPU_IRQ_CSI2_OUT_EOF;
	else if (channel == CSI_MEM3)
		irq = IPU_IRQ_CSI3_OUT_EOF;
	else if (channel == CSI_PRP_ENC_MEM)
		irq = IPU_IRQ_PRP_ENC_OUT_EOF;
	else if (channel == CSI_PRP_VF_MEM)
		irq = IPU_IRQ_PRP_VF_OUT_EOF;
	else{
		dev_err(ipu->dev, "Not a CSI channel\n");
		return;
	}

	init_completion(&ipu->csi_comp);
	ret = ipu_request_irq(ipu, irq, csi_irq_handler, 0, NULL, ipu);
	if (ret < 0) {
		dev_err(ipu->dev, "CSI irq %d in use\n", irq);
		return;
	}
	ret = wait_for_completion_timeout(&ipu->csi_comp, msecs_to_jiffies(500));
	ipu_free_irq(ipu, irq, ipu);
	dev_dbg(ipu->dev, "CSI stop timeout - %d * 10ms\n", 5 - ret);
}

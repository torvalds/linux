/*
 $License:
    Copyright (C) 2010 InvenSense Corporation, All Rights Reserved.
 $
 */

/**
 *  @defgroup   ACCELDL (Motion Library - Accelerometer Driver Layer)
 *  @brief      Provides the interface to setup and handle an accelerometers
 *              connected to the secondary I2C interface of the gyroscope.
 *
 *  @{
 *      @file   lis331.c
 *      @brief  Accelerometer setup and handling methods for ST LIS331
 */

/* ------------------ */
/* - Include Files. - */
/* ------------------ */

#ifdef __KERNEL__
#include <linux/module.h>
#endif

#include "mpu.h"
#include "mlsl.h"
#include "mlos.h"

#include <log.h>
#undef MPL_LOG_TAG
#define MPL_LOG_TAG "MPL-acc"

/* full scale setting - register & mask */
#define LIS331_CTRL_REG1         (0x20)
#define LIS331_CTRL_REG2         (0x21)
#define LIS331_CTRL_REG3         (0x22)
#define LIS331_CTRL_REG4         (0x23)
#define LIS331_CTRL_REG5         (0x24)
#define LIS331_HP_FILTER_RESET   (0x25)
#define LIS331_REFERENCE         (0x26)
#define LIS331_STATUS_REG        (0x27)
#define LIS331_OUT_X_L           (0x28)
#define LIS331_OUT_X_H           (0x29)
#define LIS331_OUT_Y_L           (0x2a)
#define LIS331_OUT_Y_H           (0x2b)
#define LIS331_OUT_Z_L           (0x2b)
#define LIS331_OUT_Z_H           (0x2d)

#define LIS331_INT1_CFG          (0x30)
#define LIS331_INT1_SRC          (0x31)
#define LIS331_INT1_THS          (0x32)
#define LIS331_INT1_DURATION     (0x33)

#define LIS331_INT2_CFG          (0x34)
#define LIS331_INT2_SRC          (0x35)
#define LIS331_INT2_THS          (0x36)
#define LIS331_INT2_DURATION     (0x37)

#define LIS331_CTRL_MASK         (0x30)
#define LIS331_SLEEP_MASK        (0x20)

#define LIS331_MAX_DUR (0x7F)

/* --------------------- */
/* -    Variables.     - */
/* --------------------- */

struct lis331dlh_config {
	unsigned int odr;
	unsigned int fsr; /* full scale range mg */
	unsigned int ths; /* Motion no-motion thseshold mg */
	unsigned int dur; /* Motion no-motion duration ms */
	unsigned char reg_ths;
	unsigned char reg_dur;
	unsigned char ctrl_reg1;
};

struct lis331dlh_private_data {
	struct lis331dlh_config suspend;
	struct lis331dlh_config resume;
};


/*****************************************
    Accelerometer Initialization Functions
*****************************************/

void lis331dlh_set_ths(struct lis331dlh_config *config,
		long ths)
{
	if ((unsigned int) ths > 1000 * config->fsr)
		ths = (long) 1000 * config->fsr;

	if (ths < 0)
		ths = 0;

	config->ths = ths;
	config->reg_ths = (unsigned char)(long)((ths * 128L) / (config->fsr));
	MPL_LOGD("THS: %d, 0x%02x\n", config->ths, (int)config->reg_ths);
}

void lis331dlh_set_dur(struct lis331dlh_config *config,
		long dur)
{
	long reg_dur = (dur * config->odr) / 1000000L;
	config->dur = dur;

	if (reg_dur > LIS331_MAX_DUR)
		reg_dur = LIS331_MAX_DUR;

	config->reg_dur = (unsigned char) reg_dur;
	MPL_LOGD("DUR: %d, 0x%02x\n", config->dur, (int)config->reg_dur);
}

/**
 * Set the Output data rate for the particular configuration
 *
 * @param config Config to modify with new ODR
 * @param odr Output data rate in units of 1/1000Hz
 */
static void lis331dlh_set_odr(
	struct lis331dlh_config *config,
	long odr)
{
	unsigned char bits;

	if (odr > 400000) {
		config->odr = 1000000;
		bits = 0x38;
	} else if (odr > 100000) {
		config->odr = 400000;
		bits = 0x30;
	} else if (odr > 50000) {
		config->odr = 100000;
		bits = 0x28;
	} else if (odr > 10000) {
		config->odr = 50000;
		bits = 0x20;
	} else if (odr > 5000) {
		config->odr = 10000;
		bits = 0xC0;
	} else if (odr > 2000) {
		config->odr = 5000;
		bits = 0xB0;
	} else if (odr > 1000) {
		config->odr = 2000;
		bits = 0x80;
	} else if (odr > 500) {
		config->odr = 1000;
		bits = 0x60;
	} else if (odr > 0) {
		config->odr = 500;
		bits = 0x40;
	} else {
		config->odr = 0;
		bits = 0;
	}

	config->ctrl_reg1 = bits | (config->ctrl_reg1 & 0x7);
	lis331dlh_set_dur(config, config->dur);
	MPL_LOGD("ODR: %d, 0x%02x\n", config->odr, (int)config->ctrl_reg1);
}

static void lis331dlh_set_fsr(
	struct lis331dlh_config *config,
	long fsr)
{
	if (fsr <= 2048)
		config->fsr = 2048;
	else if (fsr <= 4096)
		config->fsr = 4096;
	else
		config->fsr = 8192;

	lis331dlh_set_ths(config, config->ths);
	MPL_LOGD("FSR: %d\n", config->fsr);
}

int lis331dlh_suspend(void *mlsl_handle,
		      struct ext_slave_descr *slave,
		      struct ext_slave_platform_data *pdata)
{
	int result;
	unsigned char reg;
	struct lis331dlh_private_data *private_data = pdata->private_data;

	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				       LIS331_CTRL_REG1,
				       private_data->suspend.ctrl_reg1);

	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				       LIS331_CTRL_REG2, 0x0f);
	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				       LIS331_CTRL_REG3, 0x00);
	reg = 0x40;
	if (private_data->suspend.fsr == 8192)
		reg |= 0x30;
	else if (private_data->suspend.fsr == 4096)
		reg |= 0x10;
	/* else bits [4..5] are already zero */

	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				       LIS331_CTRL_REG4, reg);
	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				       LIS331_INT1_THS,
				       private_data->suspend.reg_ths);
	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				       LIS331_INT1_DURATION,
				       private_data->suspend.reg_dur);
	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				       LIS331_INT1_CFG, 0x2a);
	result = MLSLSerialRead(mlsl_handle, pdata->address,
				LIS331_HP_FILTER_RESET, 1, &reg);
	return result;
}

int lis331dlh_resume(void *mlsl_handle,
		     struct ext_slave_descr *slave,
		     struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;
	unsigned char reg;
	struct lis331dlh_private_data *private_data = pdata->private_data;

	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				       LIS331_CTRL_REG1,
				       private_data->resume.ctrl_reg1);
	ERROR_CHECK(result);
	MLOSSleep(6);

	/* Full Scale */
	reg = 0x40;
	reg &= ~LIS331_CTRL_MASK;
	if (private_data->resume.fsr == 8192)
		reg |= 0x30;
	else if (private_data->resume.fsr == 4096)
		reg |= 0x10;

	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				       LIS331_CTRL_REG4, reg);
	ERROR_CHECK(result);

	/* Configure high pass filter */
	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				       LIS331_CTRL_REG2, 0x0F);
	ERROR_CHECK(result);
	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				       LIS331_CTRL_REG3, 0x00);
	ERROR_CHECK(result);
	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				       LIS331_INT1_THS, 0x02);
	ERROR_CHECK(result);
	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				       LIS331_INT1_DURATION, 0x7F);
	ERROR_CHECK(result);
	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				       LIS331_INT1_CFG, 0x95);
	ERROR_CHECK(result);
	result = MLSLSerialRead(mlsl_handle, pdata->address,
				LIS331_HP_FILTER_RESET, 1, &reg);
	ERROR_CHECK(result);
	return result;
}

int lis331dlh_read(void *mlsl_handle,
		   struct ext_slave_descr *slave,
		   struct ext_slave_platform_data *pdata,
		   unsigned char *data)
{
	return ML_ERROR_FEATURE_NOT_IMPLEMENTED;
}

static int lis331dlh_init(void *mlsl_handle,
			  struct ext_slave_descr *slave,
			  struct ext_slave_platform_data *pdata)
{
	struct lis331dlh_private_data *private_data;
	private_data = (struct lis331dlh_private_data *)
		MLOSMalloc(sizeof(struct lis331dlh_private_data));

	if (!private_data)
		return ML_ERROR_MEMORY_EXAUSTED;

	pdata->private_data = private_data;

	private_data->resume.ctrl_reg1 = 0x37;
	private_data->suspend.ctrl_reg1 = 0x47;

	lis331dlh_set_odr(&private_data->suspend, 50000);
	lis331dlh_set_odr(&private_data->resume, 200000);
	lis331dlh_set_fsr(&private_data->suspend, 2048);
	lis331dlh_set_fsr(&private_data->resume, 2048);
	lis331dlh_set_ths(&private_data->suspend, 80);
	lis331dlh_set_ths(&private_data->resume, 40);
	lis331dlh_set_dur(&private_data->suspend, 1000);
	lis331dlh_set_dur(&private_data->resume,  2540);
	return ML_SUCCESS;
}

static int lis331dlh_exit(void *mlsl_handle,
			  struct ext_slave_descr *slave,
			  struct ext_slave_platform_data *pdata)
{
	if (pdata->private_data)
		return MLOSFree(pdata->private_data);
	else
		return ML_SUCCESS;
}

static int lis331dlh_config(void *mlsl_handle,
			struct ext_slave_descr *slave,
			struct ext_slave_platform_data *pdata,
			struct ext_slave_config *data)
{
	struct lis331dlh_private_data *private_data = pdata->private_data;
	if (!data->data)
		return ML_ERROR_INVALID_PARAMETER;

	switch (data->key) {
	case MPU_SLAVE_CONFIG_ODR_SUSPEND:
		lis331dlh_set_odr(&private_data->suspend,
				*((long *)data->data));
		break;
	case MPU_SLAVE_CONFIG_ODR_RESUME:
		lis331dlh_set_odr(&private_data->resume,
				*((long *)data->data));
		break;
	case MPU_SLAVE_CONFIG_FSR_SUSPEND:
		lis331dlh_set_fsr(&private_data->suspend,
				*((long *)data->data));
		break;
	case MPU_SLAVE_CONFIG_FSR_RESUME:
		lis331dlh_set_fsr(&private_data->resume,
				*((long *)data->data));
		break;
	case MPU_SLAVE_CONFIG_MOT_THS:
		lis331dlh_set_ths(&private_data->suspend,
				*((long *)data->data));
		break;
	case MPU_SLAVE_CONFIG_NMOT_THS:
		lis331dlh_set_ths(&private_data->resume,
				*((long *)data->data));
		break;
	case MPU_SLAVE_CONFIG_MOT_DUR:
		lis331dlh_set_dur(&private_data->suspend,
				*((long *)data->data));
		break;
	case MPU_SLAVE_CONFIG_NMOT_DUR:
		lis331dlh_set_dur(&private_data->resume,
				*((long *)data->data));
		break;
	default:
		return ML_ERROR_FEATURE_NOT_IMPLEMENTED;
	};

	return ML_SUCCESS;
}

struct ext_slave_descr lis331dlh_descr = {
	/*.init             = */ lis331dlh_init,
	/*.exit             = */ lis331dlh_exit,
	/*.suspend          = */ lis331dlh_suspend,
	/*.resume           = */ lis331dlh_resume,
	/*.read             = */ lis331dlh_read,
	/*.config           = */ lis331dlh_config,
	/*.name             = */ "lis331dlh",
	/*.type             = */ EXT_SLAVE_TYPE_ACCELEROMETER,
	/*.id               = */ ACCEL_ID_LIS331,
	/*.reg              = */ (0x28 | 0x80), /* 0x80 for burst reads */
	/*.len              = */ 6,
	/*.endian           = */ EXT_SLAVE_BIG_ENDIAN,
	/*.range            = */ {2, 480},
};

struct ext_slave_descr *lis331dlh_get_slave_descr(void)
{
	return &lis331dlh_descr;
}
EXPORT_SYMBOL(lis331dlh_get_slave_descr);

/**
 *  @}
**/

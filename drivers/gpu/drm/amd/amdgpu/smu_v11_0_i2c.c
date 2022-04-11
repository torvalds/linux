/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
 */

#include "smuio/smuio_11_0_0_offset.h"
#include "smuio/smuio_11_0_0_sh_mask.h"

#include "smu_v11_0_i2c.h"
#include "amdgpu.h"
#include "soc15_common.h"
#include <drm/drm_fixed.h>
#include <drm/drm_drv.h>
#include "amdgpu_amdkfd.h"
#include <linux/i2c.h>
#include <linux/pci.h>
#include "amdgpu_ras.h"

/* error codes */
#define I2C_OK                0
#define I2C_NAK_7B_ADDR_NOACK 1
#define I2C_NAK_TXDATA_NOACK  2
#define I2C_TIMEOUT           4
#define I2C_SW_TIMEOUT        8
#define I2C_ABORT             0x10

/* I2C transaction flags */
#define I2C_NO_STOP	1
#define I2C_RESTART	2

#define to_amdgpu_device(x) (container_of(x, struct amdgpu_ras, eeprom_control.eeprom_accessor))->adev
#define to_eeprom_control(x) container_of(x, struct amdgpu_ras_eeprom_control, eeprom_accessor)

static void smu_v11_0_i2c_set_clock_gating(struct i2c_adapter *control, bool en)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);
	uint32_t reg = RREG32_SOC15(SMUIO, 0, mmSMUIO_PWRMGT);

	reg = REG_SET_FIELD(reg, SMUIO_PWRMGT, i2c_clk_gate_en, en ? 1 : 0);
	WREG32_SOC15(SMUIO, 0, mmSMUIO_PWRMGT, reg);
}


static void smu_v11_0_i2c_enable(struct i2c_adapter *control, bool enable)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);

	WREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_ENABLE, enable ? 1 : 0);
}

static void smu_v11_0_i2c_clear_status(struct i2c_adapter *control)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);
	/* do */
	{
		RREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_CLR_INTR);

	} /* while (reg_CKSVII2C_ic_clr_intr == 0) */
}

static void smu_v11_0_i2c_configure(struct i2c_adapter *control)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);
	uint32_t reg = 0;

	reg = REG_SET_FIELD(reg, CKSVII2C_IC_CON, IC_SLAVE_DISABLE, 1);
	reg = REG_SET_FIELD(reg, CKSVII2C_IC_CON, IC_RESTART_EN, 1);
	reg = REG_SET_FIELD(reg, CKSVII2C_IC_CON, IC_10BITADDR_MASTER, 0);
	reg = REG_SET_FIELD(reg, CKSVII2C_IC_CON, IC_10BITADDR_SLAVE, 0);
	/* Standard mode */
	reg = REG_SET_FIELD(reg, CKSVII2C_IC_CON, IC_MAX_SPEED_MODE, 2);
	reg = REG_SET_FIELD(reg, CKSVII2C_IC_CON, IC_MASTER_MODE, 1);

	WREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_CON, reg);
}

static void smu_v11_0_i2c_set_clock(struct i2c_adapter *control)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);

	/*
	 * Standard mode speed, These values are taken from SMUIO MAS,
	 * but are different from what is given is
	 * Synopsys spec. The values here are based on assumption
	 * that refclock is 100MHz
	 *
	 * Configuration for standard mode; Speed = 100kbps
	 * Scale linearly, for now only support standard speed clock
	 * This will work only with 100M ref clock
	 *
	 * TBD:Change the calculation to take into account ref clock values also.
	 */

	WREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_FS_SPKLEN, 2);
	WREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_SS_SCL_HCNT, 120);
	WREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_SS_SCL_LCNT, 130);
	WREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_SDA_HOLD, 20);
}

static void smu_v11_0_i2c_set_address(struct i2c_adapter *control, uint8_t address)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);

	/* Convert fromr 8-bit to 7-bit address */
	address >>= 1;
	WREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_TAR, (address & 0xFF));
}

static uint32_t smu_v11_0_i2c_poll_tx_status(struct i2c_adapter *control)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);
	uint32_t ret = I2C_OK;
	uint32_t reg, reg_c_tx_abrt_source;

	/*Check if transmission is completed */
	unsigned long  timeout_counter = jiffies + msecs_to_jiffies(20);

	do {
		if (time_after(jiffies, timeout_counter)) {
			ret |= I2C_SW_TIMEOUT;
			break;
		}

		reg = RREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_STATUS);

	} while (REG_GET_FIELD(reg, CKSVII2C_IC_STATUS, TFE) == 0);

	if (ret != I2C_OK)
		return ret;

	/* This only checks if NAK is received and transaction got aborted */
	reg = RREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_INTR_STAT);

	if (REG_GET_FIELD(reg, CKSVII2C_IC_INTR_STAT, R_TX_ABRT) == 1) {
		reg_c_tx_abrt_source = RREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_TX_ABRT_SOURCE);
		DRM_INFO("TX was terminated, IC_TX_ABRT_SOURCE val is:%x", reg_c_tx_abrt_source);

		/* Check for stop due to NACK */
		if (REG_GET_FIELD(reg_c_tx_abrt_source,
				  CKSVII2C_IC_TX_ABRT_SOURCE,
				  ABRT_TXDATA_NOACK) == 1) {

			ret |= I2C_NAK_TXDATA_NOACK;

		} else if (REG_GET_FIELD(reg_c_tx_abrt_source,
					 CKSVII2C_IC_TX_ABRT_SOURCE,
					 ABRT_7B_ADDR_NOACK) == 1) {

			ret |= I2C_NAK_7B_ADDR_NOACK;
		} else {
			ret |= I2C_ABORT;
		}

		smu_v11_0_i2c_clear_status(control);
	}

	return ret;
}

static uint32_t smu_v11_0_i2c_poll_rx_status(struct i2c_adapter *control)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);
	uint32_t ret = I2C_OK;
	uint32_t reg_ic_status, reg_c_tx_abrt_source;

	reg_c_tx_abrt_source = RREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_TX_ABRT_SOURCE);

	/* If slave is not present */
	if (REG_GET_FIELD(reg_c_tx_abrt_source,
			  CKSVII2C_IC_TX_ABRT_SOURCE,
			  ABRT_7B_ADDR_NOACK) == 1) {
		ret |= I2C_NAK_7B_ADDR_NOACK;

		smu_v11_0_i2c_clear_status(control);
	} else {  /* wait till some data is there in RXFIFO */
		/* Poll for some byte in RXFIFO */
		unsigned long  timeout_counter = jiffies + msecs_to_jiffies(20);

		do {
			if (time_after(jiffies, timeout_counter)) {
				ret |= I2C_SW_TIMEOUT;
				break;
			}

			reg_ic_status = RREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_STATUS);

		} while (REG_GET_FIELD(reg_ic_status, CKSVII2C_IC_STATUS, RFNE) == 0);
	}

	return ret;
}




/**
 * smu_v11_0_i2c_transmit - Send a block of data over the I2C bus to a slave device.
 *
 * @address: The I2C address of the slave device.
 * @data: The data to transmit over the bus.
 * @numbytes: The amount of data to transmit.
 * @i2c_flag: Flags for transmission
 *
 * Returns 0 on success or error.
 */
static uint32_t smu_v11_0_i2c_transmit(struct i2c_adapter *control,
				  uint8_t address, uint8_t *data,
				  uint32_t numbytes, uint32_t i2c_flag)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);
	uint32_t bytes_sent, reg, ret = 0;
	unsigned long  timeout_counter;

	bytes_sent = 0;

	DRM_DEBUG_DRIVER("I2C_Transmit(), address = %x, bytes = %d , data: ",
		 (uint16_t)address, numbytes);

	if (drm_debug & DRM_UT_DRIVER) {
		print_hex_dump(KERN_INFO, "data: ", DUMP_PREFIX_NONE,
			       16, 1, data, numbytes, false);
	}

	/* Set the I2C slave address */
	smu_v11_0_i2c_set_address(control, address);
	/* Enable I2C */
	smu_v11_0_i2c_enable(control, true);

	/* Clear status bits */
	smu_v11_0_i2c_clear_status(control);


	timeout_counter = jiffies + msecs_to_jiffies(20);

	while (numbytes > 0) {
		reg = RREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_STATUS);
		if (REG_GET_FIELD(reg, CKSVII2C_IC_STATUS, TFNF)) {
			do {
				reg = 0;
				/*
				 * Prepare transaction, no need to set RESTART. I2C engine will send
				 * START as soon as it sees data in TXFIFO
				 */
				if (bytes_sent == 0)
					reg = REG_SET_FIELD(reg, CKSVII2C_IC_DATA_CMD, RESTART,
							    (i2c_flag & I2C_RESTART) ? 1 : 0);
				reg = REG_SET_FIELD(reg, CKSVII2C_IC_DATA_CMD, DAT, data[bytes_sent]);

				/* determine if we need to send STOP bit or not */
				if (numbytes == 1)
					/* Final transaction, so send stop unless I2C_NO_STOP */
					reg = REG_SET_FIELD(reg, CKSVII2C_IC_DATA_CMD, STOP,
							    (i2c_flag & I2C_NO_STOP) ? 0 : 1);
				/* Write */
				reg = REG_SET_FIELD(reg, CKSVII2C_IC_DATA_CMD, CMD, 0);
				WREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_DATA_CMD, reg);

				/* Record that the bytes were transmitted */
				bytes_sent++;
				numbytes--;

				reg = RREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_STATUS);

			} while (numbytes &&  REG_GET_FIELD(reg, CKSVII2C_IC_STATUS, TFNF));
		}

		/*
		 * We waited too long for the transmission FIFO to become not-full.
		 * Exit the loop with error.
		 */
		if (time_after(jiffies, timeout_counter)) {
			ret |= I2C_SW_TIMEOUT;
			goto Err;
		}
	}

	ret = smu_v11_0_i2c_poll_tx_status(control);

Err:
	/* Any error, no point in proceeding */
	if (ret != I2C_OK) {
		if (ret & I2C_SW_TIMEOUT)
			DRM_ERROR("TIMEOUT ERROR !!!");

		if (ret & I2C_NAK_7B_ADDR_NOACK)
			DRM_ERROR("Received I2C_NAK_7B_ADDR_NOACK !!!");


		if (ret & I2C_NAK_TXDATA_NOACK)
			DRM_ERROR("Received I2C_NAK_TXDATA_NOACK !!!");
	}

	return ret;
}


/**
 * smu_v11_0_i2c_receive - Receive a block of data over the I2C bus from a slave device.
 *
 * @address: The I2C address of the slave device.
 * @numbytes: The amount of data to transmit.
 * @i2c_flag: Flags for transmission
 *
 * Returns 0 on success or error.
 */
static uint32_t smu_v11_0_i2c_receive(struct i2c_adapter *control,
				 uint8_t address, uint8_t *data,
				 uint32_t numbytes, uint8_t i2c_flag)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);
	uint32_t bytes_received, ret = I2C_OK;

	bytes_received = 0;

	/* Set the I2C slave address */
	smu_v11_0_i2c_set_address(control, address);

	/* Enable I2C */
	smu_v11_0_i2c_enable(control, true);

	while (numbytes > 0) {
		uint32_t reg = 0;

		smu_v11_0_i2c_clear_status(control);


		/* Prepare transaction */

		/* Each time we disable I2C, so this is not a restart */
		if (bytes_received == 0)
			reg = REG_SET_FIELD(reg, CKSVII2C_IC_DATA_CMD, RESTART,
					    (i2c_flag & I2C_RESTART) ? 1 : 0);

		reg = REG_SET_FIELD(reg, CKSVII2C_IC_DATA_CMD, DAT, 0);
		/* Read */
		reg = REG_SET_FIELD(reg, CKSVII2C_IC_DATA_CMD, CMD, 1);

		/* Transmitting last byte */
		if (numbytes == 1)
			/* Final transaction, so send stop if requested */
			reg = REG_SET_FIELD(reg, CKSVII2C_IC_DATA_CMD, STOP,
					    (i2c_flag & I2C_NO_STOP) ? 0 : 1);

		WREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_DATA_CMD, reg);

		ret = smu_v11_0_i2c_poll_rx_status(control);

		/* Any error, no point in proceeding */
		if (ret != I2C_OK) {
			if (ret & I2C_SW_TIMEOUT)
				DRM_ERROR("TIMEOUT ERROR !!!");

			if (ret & I2C_NAK_7B_ADDR_NOACK)
				DRM_ERROR("Received I2C_NAK_7B_ADDR_NOACK !!!");

			if (ret & I2C_NAK_TXDATA_NOACK)
				DRM_ERROR("Received I2C_NAK_TXDATA_NOACK !!!");

			break;
		}

		reg = RREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_DATA_CMD);
		data[bytes_received] = REG_GET_FIELD(reg, CKSVII2C_IC_DATA_CMD, DAT);

		/* Record that the bytes were received */
		bytes_received++;
		numbytes--;
	}

	DRM_DEBUG_DRIVER("I2C_Receive(), address = %x, bytes = %d, data :",
		  (uint16_t)address, bytes_received);

	if (drm_debug & DRM_UT_DRIVER) {
		print_hex_dump(KERN_INFO, "data: ", DUMP_PREFIX_NONE,
			       16, 1, data, bytes_received, false);
	}

	return ret;
}

static void smu_v11_0_i2c_abort(struct i2c_adapter *control)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);
	uint32_t reg = 0;

	/* Enable I2C engine; */
	reg = REG_SET_FIELD(reg, CKSVII2C_IC_ENABLE, ENABLE, 1);
	WREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_ENABLE, reg);

	/* Abort previous transaction */
	reg = REG_SET_FIELD(reg, CKSVII2C_IC_ENABLE, ABORT, 1);
	WREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_ENABLE, reg);

	DRM_DEBUG_DRIVER("I2C_Abort() Done.");
}


static bool smu_v11_0_i2c_activity_done(struct i2c_adapter *control)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);

	const uint32_t IDLE_TIMEOUT = 1024;
	uint32_t timeout_count = 0;
	uint32_t reg_ic_enable, reg_ic_enable_status, reg_ic_clr_activity;

	reg_ic_enable_status = RREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_ENABLE_STATUS);
	reg_ic_enable = RREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_ENABLE);


	if ((REG_GET_FIELD(reg_ic_enable, CKSVII2C_IC_ENABLE, ENABLE) == 0) &&
	    (REG_GET_FIELD(reg_ic_enable_status, CKSVII2C_IC_ENABLE_STATUS, IC_EN) == 1)) {
		/*
		 * Nobody is using I2C engine, but engine remains active because
		 * someone missed to send STOP
		 */
		smu_v11_0_i2c_abort(control);
	} else if (REG_GET_FIELD(reg_ic_enable, CKSVII2C_IC_ENABLE, ENABLE) == 0) {
		/* Nobody is using I2C engine */
		return true;
	}

	/* Keep reading activity bit until it's cleared */
	do {
		reg_ic_clr_activity = RREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_CLR_ACTIVITY);

		if (REG_GET_FIELD(reg_ic_clr_activity,
		    CKSVII2C_IC_CLR_ACTIVITY, CLR_ACTIVITY) == 0)
			return true;

		++timeout_count;

	} while (timeout_count < IDLE_TIMEOUT);

	return false;
}

static void smu_v11_0_i2c_init(struct i2c_adapter *control)
{
	/* Disable clock gating */
	smu_v11_0_i2c_set_clock_gating(control, false);

	if (!smu_v11_0_i2c_activity_done(control))
		DRM_WARN("I2C busy !");

	/* Disable I2C */
	smu_v11_0_i2c_enable(control, false);

	/* Configure I2C to operate as master and in standard mode */
	smu_v11_0_i2c_configure(control);

	/* Initialize the clock to 50 kHz default */
	smu_v11_0_i2c_set_clock(control);

}

static void smu_v11_0_i2c_fini(struct i2c_adapter *control)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);
	uint32_t reg_ic_enable_status, reg_ic_enable;

	smu_v11_0_i2c_enable(control, false);

	/* Double check if disabled, else force abort */
	reg_ic_enable_status = RREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_ENABLE_STATUS);
	reg_ic_enable = RREG32_SOC15(SMUIO, 0, mmCKSVII2C_IC_ENABLE);

	if ((REG_GET_FIELD(reg_ic_enable, CKSVII2C_IC_ENABLE, ENABLE) == 0) &&
	    (REG_GET_FIELD(reg_ic_enable_status,
			   CKSVII2C_IC_ENABLE_STATUS, IC_EN) == 1)) {
		/*
		 * Nobody is using I2C engine, but engine remains active because
		 * someone missed to send STOP
		 */
		smu_v11_0_i2c_abort(control);
	}

	/* Restore clock gating */

	/*
	 * TODO Reenabling clock gating seems to break subsequent SMU operation
	 *      on the I2C bus. My guess is that SMU doesn't disable clock gating like
	 *      we do here before working with the bus. So for now just don't restore
	 *      it but later work with SMU to see if they have this issue and can
	 *      update their code appropriately
	 */
	/* smu_v11_0_i2c_set_clock_gating(control, true); */

}

static bool smu_v11_0_i2c_bus_lock(struct i2c_adapter *control)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);

	/* Send  PPSMC_MSG_RequestI2CBus */
	if (!adev->powerplay.pp_funcs->smu_i2c_bus_access)
		goto Fail;


	if (!adev->powerplay.pp_funcs->smu_i2c_bus_access(adev->powerplay.pp_handle, true))
		return true;

Fail:
	return false;
}

static bool smu_v11_0_i2c_bus_unlock(struct i2c_adapter *control)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);

	/* Send  PPSMC_MSG_RequestI2CBus */
	if (!adev->powerplay.pp_funcs->smu_i2c_bus_access)
		goto Fail;

	/* Send  PPSMC_MSG_ReleaseI2CBus */
	if (!adev->powerplay.pp_funcs->smu_i2c_bus_access(adev->powerplay.pp_handle,
							     false))
		return true;

Fail:
	return false;
}

/***************************** EEPROM I2C GLUE ****************************/

static uint32_t smu_v11_0_i2c_eeprom_read_data(struct i2c_adapter *control,
					       uint8_t address,
					       uint8_t *data,
					       uint32_t numbytes)
{
	uint32_t  ret = 0;

	/* First 2 bytes are dummy write to set EEPROM address */
	ret = smu_v11_0_i2c_transmit(control, address, data, 2, I2C_NO_STOP);
	if (ret != I2C_OK)
		goto Fail;

	/* Now read data starting with that address */
	ret = smu_v11_0_i2c_receive(control, address, data + 2, numbytes - 2,
				    I2C_RESTART);

Fail:
	if (ret != I2C_OK)
		DRM_ERROR("ReadData() - I2C error occurred :%x", ret);

	return ret;
}

static uint32_t smu_v11_0_i2c_eeprom_write_data(struct i2c_adapter *control,
						uint8_t address,
						uint8_t *data,
						uint32_t numbytes)
{
	uint32_t  ret;

	ret = smu_v11_0_i2c_transmit(control, address, data, numbytes, 0);

	if (ret != I2C_OK)
		DRM_ERROR("WriteI2CData() - I2C error occurred :%x", ret);
	else
		/*
		 * According to EEPROM spec there is a MAX of 10 ms required for
		 * EEPROM to flush internal RX buffer after STOP was issued at the
		 * end of write transaction. During this time the EEPROM will not be
		 * responsive to any more commands - so wait a bit more.
		 *
		 * TODO Improve to wait for first ACK for slave address after
		 * internal write cycle done.
		 */
		msleep(10);

	return ret;

}

static void lock_bus(struct i2c_adapter *i2c, unsigned int flags)
{
	struct amdgpu_ras_eeprom_control *control = to_eeprom_control(i2c);

	if (!smu_v11_0_i2c_bus_lock(i2c)) {
		DRM_ERROR("Failed to lock the bus from SMU");
		return;
	}

	control->bus_locked = true;
}

static int trylock_bus(struct i2c_adapter *i2c, unsigned int flags)
{
	WARN_ONCE(1, "This operation not supposed to run in atomic context!");
	return false;
}

static void unlock_bus(struct i2c_adapter *i2c, unsigned int flags)
{
	struct amdgpu_ras_eeprom_control *control = to_eeprom_control(i2c);

	if (!smu_v11_0_i2c_bus_unlock(i2c)) {
		DRM_ERROR("Failed to unlock the bus from SMU");
		return;
	}

	control->bus_locked = false;
}

static const struct i2c_lock_operations smu_v11_0_i2c_i2c_lock_ops = {
	.lock_bus = lock_bus,
	.trylock_bus = trylock_bus,
	.unlock_bus = unlock_bus,
};

static int smu_v11_0_i2c_eeprom_i2c_xfer(struct i2c_adapter *i2c_adap,
			      struct i2c_msg *msgs, int num)
{
	int i, ret;
	struct amdgpu_ras_eeprom_control *control = to_eeprom_control(i2c_adap);

	if (!control->bus_locked) {
		DRM_ERROR("I2C bus unlocked, stopping transaction!");
		return -EIO;
	}

	smu_v11_0_i2c_init(i2c_adap);

	for (i = 0; i < num; i++) {
		if (msgs[i].flags & I2C_M_RD)
			ret = smu_v11_0_i2c_eeprom_read_data(i2c_adap,
							(uint8_t)msgs[i].addr,
							msgs[i].buf, msgs[i].len);
		else
			ret = smu_v11_0_i2c_eeprom_write_data(i2c_adap,
							 (uint8_t)msgs[i].addr,
							 msgs[i].buf, msgs[i].len);

		if (ret != I2C_OK) {
			num = -EIO;
			break;
		}
	}

	smu_v11_0_i2c_fini(i2c_adap);
	return num;
}

static u32 smu_v11_0_i2c_eeprom_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}


static const struct i2c_algorithm smu_v11_0_i2c_eeprom_i2c_algo = {
	.master_xfer = smu_v11_0_i2c_eeprom_i2c_xfer,
	.functionality = smu_v11_0_i2c_eeprom_i2c_func,
};

int smu_v11_0_i2c_eeprom_control_init(struct i2c_adapter *control)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);
	int res;

	control->owner = THIS_MODULE;
	control->class = I2C_CLASS_SPD;
	control->dev.parent = &adev->pdev->dev;
	control->algo = &smu_v11_0_i2c_eeprom_i2c_algo;
	snprintf(control->name, sizeof(control->name), "RAS EEPROM");
	control->lock_ops = &smu_v11_0_i2c_i2c_lock_ops;

	res = i2c_add_adapter(control);
	if (res)
		DRM_ERROR("Failed to register hw i2c, err: %d\n", res);

	return res;
}

void smu_v11_0_i2c_eeprom_control_fini(struct i2c_adapter *control)
{
	i2c_del_adapter(control);
}

/*
 * Keep this for future unit test if bugs arise
 */
#if 0
#define I2C_TARGET_ADDR 0xA0

bool smu_v11_0_i2c_test_bus(struct i2c_adapter *control)
{

	uint32_t ret = I2C_OK;
	uint8_t data[6] = {0xf, 0, 0xde, 0xad, 0xbe, 0xef};


	DRM_INFO("Begin");

	if (!smu_v11_0_i2c_bus_lock(control)) {
		DRM_ERROR("Failed to lock the bus!.");
		return false;
	}

	smu_v11_0_i2c_init(control);

	/* Write 0xde to address 0x0000 on the EEPROM */
	ret = smu_v11_0_i2c_eeprom_write_data(control, I2C_TARGET_ADDR, data, 6);

	ret = smu_v11_0_i2c_eeprom_read_data(control, I2C_TARGET_ADDR, data, 6);

	smu_v11_0_i2c_fini(control);

	smu_v11_0_i2c_bus_unlock(control);


	DRM_INFO("End");
	return true;
}
#endif

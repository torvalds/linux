#ifndef _DRXK_H_
#define _DRXK_H_

#include <linux/kconfig.h>
#include <linux/types.h>
#include <linux/i2c.h>

/**
 * struct drxk_config - Configure the initial parameters for DRX-K
 *
 * @adr:		I2C Address of the DRX-K
 * @parallel_ts:	True means that the device uses parallel TS,
 * 			Serial otherwise.
 * @dynamic_clk:	True means that the clock will be dynamically
 *			adjusted. Static clock otherwise.
 * @enable_merr_cfg:	Enable SIO_PDR_PERR_CFG/SIO_PDR_MVAL_CFG.
 * @single_master:	Device is on the single master mode
 * @no_i2c_bridge:	Don't switch the I2C bridge to talk with tuner
 * @antenna_gpio:	GPIO bit used to control the antenna
 * @antenna_dvbt:	GPIO bit for changing antenna to DVB-C. A value of 1
 *			means that 1=DVBC, 0 = DVBT. Zero means the opposite.
 * @mpeg_out_clk_strength: DRXK Mpeg output clock drive strength.
 * @microcode_name:	Name of the firmware file with the microcode
 * @qam_demod_parameter_count:	The number of parameters used for the command
 *				to set the demodulator parameters. All
 *				firmwares are using the 2-parameter commmand.
 *				An exception is the "drxk_a3.mc" firmware,
 *				which uses the 4-parameter command.
 *				A value of 0 (default) or lower indicates that
 *				the correct number of parameters will be
 *				automatically detected.
 * @load_firmware_sync:		Force the firmware load to be synchronous.
 *
 * On the *_gpio vars, bit 0 is UIO-1, bit 1 is UIO-2 and bit 2 is
 * UIO-3.
 */
struct drxk_config {
	u8	adr;
	bool	single_master;
	bool	no_i2c_bridge;
	bool	parallel_ts;
	bool	dynamic_clk;
	bool	enable_merr_cfg;
	bool	load_firmware_sync;

	bool	antenna_dvbt;
	u16	antenna_gpio;

	u8	mpeg_out_clk_strength;
	int	chunk_size;

	const char	*microcode_name;
	int		 qam_demod_parameter_count;
};

#if IS_ENABLED(CONFIG_DVB_DRXK)
extern struct dvb_frontend *drxk_attach(const struct drxk_config *config,
					struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *drxk_attach(const struct drxk_config *config,
					struct i2c_adapter *i2c)
{
        printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
        return NULL;
}
#endif

#endif

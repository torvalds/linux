#ifndef __BACKPORT_LINUX_I2C_MUX_H
#define __BACKPORT_LINUX_I2C_MUX_H
#include_next <linux/i2c-mux.h>
#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0))
#define i2c_add_mux_adapter(parent, mux_dev, mux_priv, force_nr, chan_id, class, select, deselect) \
	i2c_add_mux_adapter(parent, mux_priv, force_nr, chan_id, select, deselect)
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0))
#define i2c_add_mux_adapter(parent, mux_dev, mux_priv, force_nr, chan_id, class, select, deselect) \
	i2c_add_mux_adapter(parent, mux_dev, mux_priv, force_nr, chan_id, select, deselect)
#endif

#endif /* __BACKPORT_LINUX_I2C_MUX_H */

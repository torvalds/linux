/*
 * arch/arm/mach-exynos/midas.h
 */

#ifndef __MIDAS_H__
#define __MIDAS_H__

static inline int i2c_add_devices(int busnum, struct i2c_board_info *infos,
				  int size)
{
	struct i2c_adapter *i2c_adap;
	int i;
	i2c_adap = i2c_get_adapter(busnum);
	if (!i2c_adap) {
		pr_err("%s: ERROR i2c bus %d not found\n", __func__, busnum);
		return -ENODEV;
	}

	for (i = 0; i < size; i++) {
		struct i2c_client *client = i2c_new_device(i2c_adap, infos + i);
		if (client)
			dev_info(&client->dev, "%s - added %s successfully\n",
				 __func__, infos[i].type);
		else
			dev_err(&i2c_adap->dev,
				"%s - added %s at bus i2c bus %d failed\n",
				__func__, infos[i].type, busnum);

	}
	i2c_put_adapter(i2c_adap);

	return 0;
}

#endif


#include <linux/it6681.h>


static struct it6681_platform_data pdata_it6681;

static struct i2c_board_info __initdata panda_i2c2_it6681[] = {
	{
		I2C_BOARD_INFO("it6681_hdmi_rx", IT6681_HDMI_RX_ADDR>>1),
                .platform_data = &pdata_it6681,
	},
	{
		I2C_BOARD_INFO("it6681_hdmi_tx", IT6681_HDMI_TX_ADDR>>1),
                .platform_data = &pdata_it6681,
	},
	{
		I2C_BOARD_INFO("it6681_mhl", IT6681_MHL_ADDR>>1),
                .platform_data = &pdata_it6681,
	},
};


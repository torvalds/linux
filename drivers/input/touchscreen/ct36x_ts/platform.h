#ifndef PLATFORM_H
#define PLATFORM_H

// Platform data
struct ct36x_platform_data {
	int 				rst;
	int 				ss;
};

extern struct i2c_driver ct36x_ts_driver;

void ct36x_ts_reg_read(struct i2c_client *client, unsigned short addr, char *buf, int len);
void ct36x_ts_reg_write(struct i2c_client *client, unsigned short addr, char *buf, int len);

void ct36x_platform_get_cfg(struct ct36x_ts_info *ct36x_ts);
int ct36x_platform_set_dev(struct ct36x_ts_info *ct36x_ts);

int ct36x_platform_get_resource(struct ct36x_ts_info *ct36x_ts);
void ct36x_platform_put_resource(struct ct36x_ts_info *ct36x_ts);

void ct36x_platform_hw_reset(struct ct36x_ts_info *ct36x_ts);
#endif

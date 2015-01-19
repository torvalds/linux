
#ifndef AMLCHIP_H
#define AMLCHIP_H
#ifndef CONFIG_OF
#include "linux/ct36x_platform.h"
#endif

#define LATE_UPGRADE
void ct36x_ts_reg_read(struct i2c_client *client, unsigned short addr, char *buf, int len);
void ct36x_ts_reg_write(struct i2c_client *client, unsigned short addr, char *buf, int len);
int ct36x_test_read(struct i2c_client *client, unsigned short addr, char *buf, int len);

void ct36x_platform_get_cfg(struct ct36x_ts_info *ct36x_ts);
int ct36x_platform_set_dev(struct ct36x_ts_info *ct36x_ts);

int ct36x_platform_get_resource(struct ct36x_ts_info *ct36x_ts);
void ct36x_platform_put_resource(struct ct36x_ts_info *ct36x_ts);
#ifdef CONFIG_OF
void ct36x_platform_hw_reset(struct ct36x_ts_info *ct36x_ts);
void ct36x_hw_reset(struct touch_pdata *pdata);
void ct36x_upgrade_touch(void);
int ct36x_late_upgrade(void *p);
void ct36x_read_version(char* ver);
#else
void ct36x_platform_hw_reset(struct ct36x_platform_data *pdata);
#endif

extern struct i2c_driver ct36x_ts_driver;
#endif


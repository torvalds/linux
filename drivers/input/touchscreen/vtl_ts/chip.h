#ifndef	_CHIP_H_
#define	_CHIP_H_

extern int chip_init(void);
extern int chip_get_fwchksum(struct i2c_client *client,int *fwchksum);
extern int chip_get_checksum(struct i2c_client *client,int *bin_checksum,int *fw_checksum);
extern int update(struct i2c_client *client);
extern int chip_update(struct i2c_client *client);
extern int chip_enter_sleep_mode(void);
extern int chip_solfware_reset(struct i2c_client *client);

#endif

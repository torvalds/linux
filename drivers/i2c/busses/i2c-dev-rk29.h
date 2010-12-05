#ifndef _I2C_DEV_H
#define _I2C_DEV_H

#define I2C_DEV_NAME		"i2c-dev"
#define I2C_DEV_PATH        "/dev/"I2C_DEV_NAME

#define MAX_ADAP_LENG		48
#define MAX_CLIENT_LENG		20
#define MAX_VALUE_NUM     	16
#define MAX_I2C_BUS			4
#define MAX_CLIENT_NUM		32



#define I2C_LIST			0X7000
#define I2C_DUMP			0x7010
#define I2C_GET				0x7020
#define I2C_SET				0x7030


struct i2c_client_info {
	int addr;
	char name[MAX_CLIENT_LENG];
};
struct i2c_adap_info {
	int id;
	char name[MAX_ADAP_LENG];
	int client_nr;
	struct i2c_client_info client[MAX_CLIENT_NUM];
};
struct i2c_list_info {
	int adap_nr;
	struct i2c_adap_info adap[MAX_I2C_BUS];
};

struct i2c_dump_info {
	int id;
	int addr;
	int get_num;
	int get_value[MAX_VALUE_NUM];
	int set_num;
	int set_value[MAX_VALUE_NUM];
};

struct i2c_get_info {
	char mode;
	int id;
	int addr;
	int reg;
	int num;
	int value[MAX_VALUE_NUM];
};
struct i2c_set_info {
	char mode;
	int id;
	int addr;
	int reg;
	int num;
	int value[MAX_VALUE_NUM];
};

#endif /* _I2CDEV_H */

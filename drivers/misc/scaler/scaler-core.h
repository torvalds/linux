#ifndef __SCALER_CORE_H__
#define __SCALER_CORE_H__
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/cdev.h>
#include <linux/file.h>
#include <linux/list.h>


struct display_edid {
	char *data;
	char *ext_data;
};

enum scaler_output_type {
	SCALER_OUT_INVALID = 0,
	SCALER_OUT_LVDS,
	SCALER_OUT_VGA,
	SCALER_OUT_RGB,
	SCALER_OUT_HDMI,
	SCALER_OUT_DP,
	SCALER_OUT_NUMS,
};

enum scaler_input_type {
	SCALER_IN_INVALID = 0,
	SCALER_IN_VGA,
	SCALER_IN_RGB,
	SCALER_IN_DVI,
	SCALER_IN_YPBPR,
	SCALER_IN_YCBCR,
	SCALER_IN_DP,
	SCALER_IN_HDMI,
	SCALER_IN_MYDP,
	SCALER_IN_IDP,
	SCALER_IN_NUMS,
};

enum scaler_bus_type {
	SCALER_BUS_TYPE_INVALID = 0,
	SCALER_BUS_TYPE_UART,
	SCALER_BUS_TYPE_I2C,
	SCALER_BUS_TYPE_SPI,
	SCALER_BUS_TYPE_NUMS,
};

/*
 * the function of scaler, for example convertor or switch 
*/
enum scaler_function_type {
	SCALER_FUNC_INVALID = 0,
	SCALER_FUNC_CONVERTOR,   //转换器
	SCALER_FUNC_SWITCH,      //切换开关多选一输出 
	SCALER_FUNC_FULL,        //全功能
	SCALER_FUNC_NUMS,
};

struct scaler_output_port {
	int id;
	int max_hres;
	int max_vres;
	int freq;
	unsigned led_gpio;   //working led
	enum scaler_output_type type;
	struct list_head next; 
};

struct scaler_input_port {
	int id;
	int max_hres;
	int max_vres;
	int freq;       //HZ
	unsigned led_gpio;   //working led
	enum scaler_input_type type;
	struct list_head next; 
};

struct scaler_chip_dev {
	char id;
	char name[I2C_NAME_SIZE];
	struct i2c_client *client;
	
	enum scaler_function_type func_type;

	int int_gpio;
	int reset_gpio;
	int power_gpio;
	int status_gpio;
	int reg_size;  //8bit = 1, 16bit = 2, 24bit = 3,32bit = 4.

	struct list_head oports; //config all support output type by make menuconfig
	struct list_head iports; //config all support input type by make menuconfig
	enum scaler_input_type cur_in_type;
	enum scaler_output_type cur_out_type;

	//init hardware(gpio etc.)
	int (*init_hw)(void);
	int (*exit_hw)(void);

	//enable chip to process image
	void (*start)(void);
	//disable chip to process image
	void (*stop)(void);
	void (*reset)(char active);
	void (*suspend)(void);
	void (*resume)(void);

	//
	int (*read)(unsigned short reg, int bytes, void *dest);
	int (*write)(unsigned short reg, int bytes, void *src);
	int (*parse_cmd)(unsigned int cmd, void *arg);
	int (*update_firmware)(void *data);

	//scaler chip dev list
	struct list_head next;
};

struct scaler_platform_data {
	int int_gpio;
	int reset_gpio;
	int power_gpio;
	int status_gpio; //check chip if work on lower power mode or normal mode.
	//config input indicator lamp
	unsigned in_leds_gpio[SCALER_IN_NUMS];  
	//config output indicator lamp
	unsigned out_leds_gpio[SCALER_OUT_NUMS];  

	char *firmware; 
	//function type
	enum scaler_function_type func_type;

	//config in and out 
	enum scaler_input_type *iports;
	enum scaler_output_type *oports;
	int iport_size;
	int oport_size;
	enum scaler_input_type default_in;
	enum scaler_output_type default_out;

	int (*init_hw)(void);
	int (*exit_hw)(void);
};

struct scaler_device {
	struct class *class;
	dev_t devno;
	struct cdev *cdev;

	struct display_edid edid;

	struct list_head chips;
};

struct scaler_chip_dev *alloc_scaler_chip(void);
//free chip memory and port memory
void free_scaler_chip(struct scaler_chip_dev *chip);
int init_scaler_chip(struct scaler_chip_dev *chip, struct scaler_platform_data *pdata);
//
int register_scaler_chip(struct scaler_chip_dev *chip);
int unregister_scaler_chip(struct scaler_chip_dev *chip);

#define SCALER_IOCTL_MAGIC 's'
#define SCALER_IOCTL_POWER _IOW(SCALER_IOCTL_MAGIC, 0x00, char)
#define SCALER_IOCTL_GET_CUR_INPUT _IOR(SCALER_IOCTL_MAGIC, 0x01, int port_id)
#define SCALER_IOCTL_SET_CUR_INPUT _IOW(SCALER_IOCTL_MAGIC, 0x02, int port_id)

#endif 

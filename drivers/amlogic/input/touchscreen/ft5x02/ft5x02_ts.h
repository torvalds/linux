#ifndef __LINUX_FT5X02_TS_H__
#define __LINUX_FT5X02_TS_H__

/*linux/i2c/ft5x02_ts.h*/
#include <linux/i2c.h>


/* -- dirver configure -- */
#define CFG_SUPPORT_TOUCH_KEY  1    /*touch key, HOME, SEARCH, RETURN etc*/

#define CFG_MAX_TOUCH_POINTS  5

#define PRESS_MAX       255
#define FT_PRESS		127

#define FT_MAX_ID	0x0F
#define FT_TOUCH_STEP	6
#define FT_TOUCH_X_H_POS		3
#define FT_TOUCH_X_L_POS		4
#define FT_TOUCH_Y_H_POS		5
#define FT_TOUCH_Y_L_POS		6
#define FT_TOUCH_EVENT_POS		3
#define FT_TOUCH_ID_POS			5


#define POINT_READ_BUF  (3 + 6 * CFG_MAX_TOUCH_POINTS) /*standard iic protocol*/

#define FT5X02_NAME		"ft5x02"

#if CFG_SUPPORT_TOUCH_KEY
#define KEY_PRESS       1
#define KEY_RELEASE     0
#define CFG_NUMOFKEYS 4   
#endif


/*register address*/

#define FTS_DBG
#ifdef FTS_DBG
#define DBG(fmt, args...) printk("[FTS]" fmt, ## args)
#else
#define DBG(fmt, args...) do{}while(0)
#endif


/* The platform data for the Focaltech ft5x0x touchscreen driver */
struct ft5x02_platform_data {
	unsigned int x_max;
	unsigned int y_max;
	unsigned long irqflags;/*default:IRQF_TRIGGER_FALLING*/
	unsigned int irq;
	unsigned int reset;
};

int ft5x02_i2c_Read(struct i2c_client *client, char *writebuf, int writelen,
		    char *readbuf, int readlen);
int ft5x02_i2c_Write(struct i2c_client *client, char *writebuf, int writelen);

int ft5x02_write_reg(struct i2c_client * client, u8 regaddr, u8 regvalue);

int ft5x02_read_reg(struct i2c_client * client, u8 regaddr, u8 *regvalue);


#endif

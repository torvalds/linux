//[*]--------------------------------------------------------------------------------------------------[*]
//
//
// 
//  I2C INA231(Sensor) driver
//  2013.07.17
// 
//
//[*]--------------------------------------------------------------------------------------------------[*]
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <linux/platform_data/ina231.h>

//[*]--------------------------------------------------------------------------------------------------[*]
#include "ina231-misc.h"
#include "ina231-sysfs.h"

//#define DEBUG_INA231
//[*]--------------------------------------------------------------------------------------------------[*]
//
// function prototype
//
//[*]--------------------------------------------------------------------------------------------------[*]
static 	void __exit		ina231_i2c_exit		(void);
static 	int __init 		ina231_i2c_init		(void);
static 	int __devexit 	ina231_i2c_remove	(struct i2c_client *client);
static 	int __devinit 	ina231_i2c_probe	(struct i2c_client *client, const struct i2c_device_id *id);
        int 	        ina231_i2c_read     (struct i2c_client *client, unsigned char cmd);
        int 	        ina231_i2c_write    (struct i2c_client *client, unsigned char cmd, unsigned short data);
        void            ina231_i2c_enable   (struct ina231_sensor *sensor);
static 	void 	        ina231_work		    (struct work_struct *work);

static enum hrtimer_restart ina231_timer    (struct hrtimer *timer);
		
//[*]--------------------------------------------------------------------------------------------------[*]
#ifdef CONFIG_PM
static int 	ina231_i2c_suspend(struct i2c_client *client, pm_message_t message)
{
	#ifdef CONFIG_HAS_EARLYSUSPEND
		struct	ina231	*sensor = i2c_get_clientdata(client);
	
		sensor->pdata->suspend(&client->dev);
	#endif

	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int 	ina231_i2c_resume(struct i2c_client *client)
{
	#ifdef CONFIG_HAS_EARLYSUSPEND
		struct	ina231	*sensor = i2c_get_clientdata(client);

		sensor->pdata->resume(&cliet->dev);
	#endif	

	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
#else
	#define ina231_i2c_suspend 	NULL
	#define ina231_i2c_resume  	NULL
#endif

//[*]--------------------------------------------------------------------------------------------------[*]
int 	ina231_i2c_read(struct i2c_client *client, unsigned char cmd)
{
	struct i2c_msg	msg[2];
	int 			ret;
	
	unsigned char   buf[2];

	memset(msg, 0x00, sizeof(msg));

	msg[0].addr 	= client->addr;
	msg[0].flags 	= 0;
	msg[0].len 		= 1;
	msg[0].buf 		= &cmd;

	msg[1].addr 	= client->addr;
	msg[1].flags    = I2C_M_RD;
	msg[1].len 		= 2;
	msg[1].buf 		= &buf[0];
	
	if ((ret = i2c_transfer(client->adapter, msg, 2)) != 2) {
		dev_err(&client->dev, "I2C read error: (%d) reg: 0x%X \n", ret, cmd);
		return -EIO;
	}

    ret = ((buf[0] << 8) | buf[1]) & 0xFFFF;
	return 	ret;
}

//[*]--------------------------------------------------------------------------------------------------[*]
int 	ina231_i2c_write(struct i2c_client *client, unsigned char cmd, unsigned short data)
{
	int 			ret;
	unsigned char	block_data[3];

	memset(block_data, 0x00, sizeof(block_data));

    block_data[0] = cmd;
    block_data[1] = (data >> 8) & 0xFF;	
    block_data[2] = (data     ) & 0xFF;	

	if ((ret = i2c_master_send(client, block_data, 3)) < 0) {
		dev_err(&client->dev, "I2C write error: (%d) reg: 0x%X \n", ret, cmd);
		return ret;
	}
	
	return ret;
}

//[*]--------------------------------------------------------------------------------------------------[*]
void    ina231_i2c_enable(struct ina231_sensor *sensor)
{
    hrtimer_start(&sensor->timer, ktime_set(sensor->timer_sec, sensor->timer_nsec), HRTIMER_MODE_REL);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static 	void 	ina231_work		(struct work_struct *work)
{
	struct ina231_sensor 	*sensor = container_of(work, struct ina231_sensor, work);

    if(sensor->pd->enable)  {
        sensor->reg_bus_volt    = ina231_i2c_read(sensor->client, REG_BUS_VOLT   );
        sensor->reg_current     = ina231_i2c_read(sensor->client, REG_CURRENT    );
    
    	mutex_lock(&sensor->mutex);
        sensor->cur_uV = sensor->reg_bus_volt * FIX_uV_LSB;
        sensor->cur_uA = sensor->reg_current * sensor->cur_lsb_uA;
        sensor->cur_uW = (sensor->cur_uV / 1000 ) * (sensor->cur_uA / 1000);
        
        if((sensor->cur_uV > sensor->max_uV) || (sensor->cur_uA > sensor->cur_uA))  {
            sensor->max_uV = sensor->cur_uV;    sensor->max_uA = sensor->cur_uA;    sensor->max_uW = sensor->cur_uW;
        }
    	mutex_unlock(&sensor->mutex);
    }
    else    {
        sensor->cur_uV = 0; sensor->cur_uA = 0; sensor->cur_uW = 0;
    }
    
#if defined(DEBUG_INA231)
    printk("%s : BUS Voltage = %06d uV, %1d.%06d V\n", sensor->pd->name, sensor->cur_uV, sensor->cur_uV/1000000, sensor->cur_uV%1000000);
    printk("%s : Curent      = %06d uA, %1d.%06d A\n", sensor->pd->name, sensor->cur_uA, sensor->cur_uA/1000000, sensor->cur_uA%1000000);
    printk("%s : Powert      = %06d uW, %1d.%06d W\n", sensor->pd->name, sensor->cur_uW, sensor->cur_uW/1000000, sensor->cur_uW%1000000);
#endif    
}

//[*]--------------------------------------------------------------------------------------------------[*]
static enum hrtimer_restart ina231_timer(struct hrtimer *timer)
{
	struct ina231_sensor 	*sensor = container_of(timer, struct ina231_sensor, timer);

    queue_work(sensor->wq, &sensor->work);
	
    if(sensor->pd->enable)  ina231_i2c_enable(sensor);
	
	return HRTIMER_NORESTART;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int __devinit 	ina231_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int     rc = 0;
    struct  ina231_sensor   *sensor;

	if(!(sensor = kzalloc(sizeof(struct ina231_sensor), GFP_KERNEL)))	{
		dev_err(&client->dev, "INA231 Sensor struct malloc error!\n");
		return	-ENOMEM;
	}

    // mutex init	
	mutex_init(&sensor->mutex);

	sensor->client	= client;
	sensor->pd  	= client->dev.platform_data;
	
	i2c_set_clientdata(client, sensor);

    // Calculate current lsb value
    sensor->cur_lsb_uA  = sensor->pd->max_A * 1000000 / 32768;
    // Calculate register value
    sensor->reg_calibration = 5120000 / (sensor->cur_lsb_uA * sensor->pd->shunt_R_mohm);

    if((rc = ina231_i2c_write(sensor->client, REG_CONFIG,      sensor->pd->config))        < 0) goto out;
    if((rc = ina231_i2c_write(sensor->client, REG_CALIBRATION, sensor->reg_calibration))   < 0) goto out;
    if((rc = ina231_i2c_write(sensor->client, REG_ALERT_EN,    0x0000))  < 0)                   goto out;
    if((rc = ina231_i2c_write(sensor->client, REG_ALERT_LIMIT, 0x0000))  < 0)                   goto out;
    
    if((rc = ina231_i2c_read(sensor->client, REG_CONFIG      )) != sensor->pd->config      )    goto out;
    if((rc = ina231_i2c_read(sensor->client, REG_CALIBRATION )) != sensor->reg_calibration )    goto out;
    if((rc = ina231_i2c_read(sensor->client, REG_ALERT_EN    )) != 0x0000)                      goto out;
    if((rc = ina231_i2c_read(sensor->client, REG_ALERT_LIMIT )) != 0x0000)                      goto out;

    // misc driver probe
    if(ina231_misc_probe(sensor) < 0)           goto out;

    // sysfs probe
    if(ina231_sysfs_create(&client->dev) < 0)   goto out;

    // timer run for sensor data receive
    INIT_WORK(&sensor->work, ina231_work);
    if((sensor->wq = create_singlethread_workqueue("ina231_wq")) == NULL)	goto out;
        
    hrtimer_init(&sensor->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    sensor->timer_sec  = sensor->pd->update_period / 1000000;
    sensor->timer_nsec = sensor->pd->update_period % 1000000;
    sensor->timer_nsec = sensor->timer_nsec * 1000;
    sensor->timer.function = ina231_timer;

    if(sensor->pd->enable)  ina231_i2c_enable(sensor);

    // display register message
    rc = 0;
    printk("============= Probe INA231 : %s ============= \n", sensor->pd->name);
    printk("SENSOR ENABLE   : %s\n"     , sensor->pd->enable ? "true" : "false");
    printk("REG CONFIG      : 0x%04X\n" , sensor->pd->config        );
    printk("REG CALIBRATION : 0x%04X\n" , sensor->reg_calibration   );
    printk("SHUNT Resister  : %d mOhm\n", sensor->pd->shunt_R_mohm  );
    printk("MAX Current     : %d A\n"   , sensor->pd->max_A         );
    printk("Current LSB uA  : %d uA\n"  , sensor->cur_lsb_uA        );
    printk("Conversion Time : %d us\n"  , sensor->pd->update_period );
    printk("=====================================================\n");
    
    return  0;
out:
    printk("============= Probe INA231 Fail! : %s (0x%04X) ============= \n", sensor->pd->name, rc); 
	kfree(sensor);
	return rc;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int __devexit	ina231_i2c_remove(struct i2c_client *client)
{
    struct  ina231_sensor   *sensor = dev_get_drvdata(&client->dev);

    // removed sysfs entry
    ina231_sysfs_remove (&client->dev);
    // removed misc drv
	ina231_misc_remove	(&client->dev);
    // timer
    if(sensor->pd->enable)  hrtimer_cancel(&sensor->timer);

	kfree(sensor);

    return  0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static const struct i2c_device_id ina231_id[] = {
	{ INA231_I2C_NAME, 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, ina231_id);

//[*]--------------------------------------------------------------------------------------------------[*]
static struct i2c_driver ina231_i2c_driver = {
	.driver = {
		.name	= INA231_I2C_NAME,
		.owner	= THIS_MODULE,
	},
	.probe		= ina231_i2c_probe,
	.remove		= __devexit_p(ina231_i2c_remove),
	.suspend	= ina231_i2c_suspend,
	.resume		= ina231_i2c_resume,
	.id_table	= ina231_id,
};

//[*]--------------------------------------------------------------------------------------------------[*]
static int __init 	ina231_i2c_init(void)
{
	return i2c_add_driver(&ina231_i2c_driver);
}
module_init(ina231_i2c_init);

//[*]--------------------------------------------------------------------------------------------------[*]
static void __exit 	ina231_i2c_exit(void)
{
	i2c_del_driver(&ina231_i2c_driver);
}
module_exit(ina231_i2c_exit);

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]

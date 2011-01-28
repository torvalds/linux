#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/stat.h>	 /* permission constants */

#include <asm/io.h>
#include <asm/sizes.h>

#include "rk29_modem.h"

struct rk29_modem_t *g_rk29_modem = NULL;

extern void rk29_host11_driver_enable(void);
extern void rk29_host11_driver_disable(void);

static ssize_t modem_status_write(struct class *cls, const char *_buf, size_t _count)
{
	struct rk29_modem_t *rk29_modem = g_rk29_modem;

    int new_mode = simple_strtoul(_buf, NULL, 16);
    printk("[%s] new_mode: %s\n", __func__, _buf);
    
    if(rk29_modem == NULL){
    	printk("!!!! g_rk29_modem is NULL !!!!\n");
    	return _count;
    }
    
    if(new_mode == rk29_modem->cur_mode){
    	printk("[%s] current already in %d mode\n", __func__, new_mode);
        return _count;
    }
    
    switch(new_mode){
    case MODEM_DISABLE:
    	if(rk29_modem->disable){
    		printk("modem disable!\n");
//    		rk29_host11_driver_disable();
  //  		mdelay(10);
    		rk29_modem->disable();
    		rk29_modem->cur_mode = new_mode;
    	}
    	break;
    	
    case MODEM_ENABLE :
    	if(rk29_modem->enable){
    		printk("modem enable!\n");
//    		rk29_host11_driver_enable();
//    		mdelay(100);
    		rk29_modem->enable();
    		rk29_modem->cur_mode = new_mode;
    	}
    	break;
    	
    case MODEM_SLEEP:
    	if(rk29_modem->sleep){
    		printk("modem sleep!\n");
    		rk29_modem->sleep();
    		rk29_modem->cur_mode = new_mode;
    	}
    	break;
    	
    default:
    	printk("[%s] invalid new mode: %d\n", __func__, new_mode);
    	break;
    }
    
	return _count;
}

static ssize_t modem_status_read(struct class *cls, char *_buf)
{
	struct rk29_modem_t *rk29_modem = g_rk29_modem;
	
//	printk("Modem type: %s, cur_mode = %d\n", rk29_modem->name, rk29_modem->cur_mode);
	
	return sprintf(_buf, "%d\n", rk29_modem->cur_mode);
}

static struct class *rk29_modem_class = NULL;
static CLASS_ATTR(modem_status, 0666, modem_status_read, modem_status_write);

int modem_is_turn_on()
{
    return (g_rk29_modem->cur_mode != 0);
}

void turn_off_modem()
{
    modem_status_write( NULL, "0", sizeof("0") );
}

void turn_on_modem()
{
    modem_status_write( NULL, "1", sizeof("1") );
}

int rk29_modem_register(struct rk29_modem_t *rk29_modem){
	int ret = -1;
	
	if(rk29_modem == NULL)
		return -1;

#if 1
	if(rk29_modem->enable) rk29_modem->enable();
#else
	if(rk29_modem->disable) rk29_modem->disable();
#endif	
	rk29_modem_class = class_create(THIS_MODULE, "rk291x_modem");
	if(rk29_modem_class == NULL){
		printk("create class rk291x_modem failed!\n");
		goto err1;
	}
	
	ret = class_create_file(rk29_modem_class, &class_attr_modem_status);
	if(ret != 0){
		printk("create rk291x_modem class file failed!\n");
		goto err2;
	}
	
	g_rk29_modem = rk29_modem;
	
	return 0;
	
err2:
	class_destroy(rk29_modem_class);
err1:
	return ret;	
}

void rk29_modem_unregister(struct rk29_modem_t *rk29_modem){
	/* disable 3G modem */		
	if(rk29_modem->disable)
		rk29_modem->disable();
		
	class_remove_file(rk29_modem_class, &class_attr_modem_status);
	class_destroy(rk29_modem_class);
	
	rk29_modem_class = NULL;
}


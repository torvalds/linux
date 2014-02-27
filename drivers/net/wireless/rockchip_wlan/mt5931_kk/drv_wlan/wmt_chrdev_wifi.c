#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <asm/current.h>
#include <asm/uaccess.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/device.h>

#include <linux/netdevice.h>
#include <net/net_namespace.h>
#include <linux/rtnetlink.h>
#include "wifi_version.h"



MODULE_LICENSE("Dual BSD/GPL");

#define WIFI_DRIVER_NAME "mtk_wmt_WIFI_chrdev"
#if WMT_PLAT_APEX
#define WIFI_DEV_MAJOR 194 // never used number
#else
#define WIFI_DEV_MAJOR 153 // never used number
#endif
#define REMOVE_MK_NODE 1
#define PFX                         "[MTK-WIFI] "
#define WIFI_LOG_DBG                  3
#define WIFI_LOG_INFO                 2
#define WIFI_LOG_WARN                 1
#define WIFI_LOG_ERR                  0


unsigned int gWIFIDbgLevel = WIFI_LOG_INFO;

#define WIFI_DBG_FUNC(fmt, arg...)    if(gWIFIDbgLevel >= WIFI_LOG_DBG){ printk(PFX "%s: "  fmt, __FUNCTION__ ,##arg);}
#define WIFI_INFO_FUNC(fmt, arg...)   if(gWIFIDbgLevel >= WIFI_LOG_INFO){ printk(PFX "%s: "  fmt, __FUNCTION__ ,##arg);}
#define WIFI_WARN_FUNC(fmt, arg...)   if(gWIFIDbgLevel >= WIFI_LOG_WARN){ printk(PFX "%s: "  fmt, __FUNCTION__ ,##arg);}
#define WIFI_ERR_FUNC(fmt, arg...)    if(gWIFIDbgLevel >= WIFI_LOG_ERR){ printk(PFX "%s: "   fmt, __FUNCTION__ ,##arg);}
#define WIFI_TRC_FUNC(f)              if(gWIFIDbgLevel >= WIFI_LOG_DBG){printk(PFX "<%s> <%d>\n", __FUNCTION__, __LINE__);}

#define VERSION "1.0"
/*mtk80707 rollback aosp hal*/
/*
 *    enable = 1, mode = 0  => init P2P network
 *    enable = 1, mode = 1  => init Soft AP network
 *    enable = 0            => uninit P2P/AP network
*/
typedef struct _PARAM_CUSTOM_P2P_SET_STRUC_T {
    unsigned int             u4Enable;
    unsigned int            u4Mode;
} PARAM_CUSTOM_P2P_SET_STRUC_T, *P_PARAM_CUSTOM_P2P_SET_STRUC_T;
/*handler of set wlan p2p mode*/
typedef int (*set_p2p_mode)(struct net_device * netdev, PARAM_CUSTOM_P2P_SET_STRUC_T p2pmode);

enum {
	WLAN_MODE_HALT,
	WLAN_MODE_AP,
	WLAN_MODE_STA_P2P,
	WLAN_MODE_MAX
};
#define WLAN_IFACE_NAME "wlan0"
#define WLAN_LEG_IFACE_NAME "legacy_wlan0"
volatile int wlan_mode = WLAN_MODE_HALT;
volatile set_p2p_mode pf_set_p2p_mode;
volatile int power_state = 0; /*0 power off, 1 power on*/
volatile int PowerOnIFname = 0;/*0 wlan0, 1 legacy_wlan0*/
EXPORT_SYMBOL(power_state);
EXPORT_SYMBOL(wlan_mode);
EXPORT_SYMBOL(pf_set_p2p_mode);
EXPORT_SYMBOL(PowerOnIFname);


void register_set_p2p_mode_handler(set_p2p_mode handler) {
	WIFI_INFO_FUNC("(pid %d) register set p2p mode handler %p\n", current->pid, handler);
	pf_set_p2p_mode = handler;
}



/*endof mtk80707*/

static int WIFI_devs = 1;        /* device count */
static int WIFI_major = WIFI_DEV_MAJOR;       /* dynamic allocation */
module_param(WIFI_major, uint, 0);
static struct cdev WIFI_cdev;
volatile int retflag_wifi = 0;
static struct semaphore wr_mtx;
static int flagIsIFchanged =0;


#include <mach/gpio.h>
//#include <linux/gpio.h>
static int _mt5931_powered_on = 0; 
static int _mt5931_32k_on = 0; 
#define MT5931_OFF_TIME 10
#define MT5931_PMU_EN_TIME 3
#define MT5931_RST_TIME 12
#define GPIO_MT5931_PMU_EN 43
#define GPIO_MT5931_RST 133

//static signed int gpio_bt_dis = -1;

extern int omap_mmc_update_mtk_card_status(int state);	

extern int rk29sdk_wifi_power(int on);
extern int rk29sdk_wifi_set_carddetect(int val);

static int mt5931_power_on(void) 
{ 

	int ret = 0;
    if(_mt5931_powered_on == 0) 
	{
        _mt5931_powered_on = 1;         
        printk(KERN_INFO "[mt5931_power_on] config EINT_PIN\n"); 
        rk29sdk_wifi_power(1);
		msleep(MT5931_RST_TIME);
    }else{
    	printk("mt5931 unmatched power on\n");
    } 
	return 0; 
}
int mt5931_power_off(void) 
{ 
    printk(KERN_INFO "[mt5931_power_off] ++\n"); 
    if(_mt5931_powered_on == 1) { 
		_mt5931_powered_on = 0; 
    rk29sdk_wifi_power(0);
	msleep(MT5931_RST_TIME);	
    }else{
    	printk("mt5931 unmatched power off\n");
    }

    printk(KERN_INFO "[mt5931_power_off] --\n"); 
    return 0; 
}

bool mtk_wcn_wifi_func_on()
{
    printk("=======================================================\n");
    printk("==== Launching Wi-Fi driver! (Powered by Rockchip) ====\n");
    printk("=======================================================\n");
    printk("MT5931 SDIO WiFi driver (Powered by Rockchip,Ver %s) init.\n", MT5931_DRV_VERSION);

mt5931_power_on();
rk29sdk_wifi_set_carddetect(1);
return true;
}
bool mtk_wcn_wifi_func_off()
{
rk29sdk_wifi_set_carddetect(0);
/* remove to wlan_remove to safely close the wifi , avoid sdio access error*/
//mt5931_power_off();
return true;
}



static int WIFI_open(struct inode *inode, struct file *file)
{
    WIFI_INFO_FUNC("%s: major %d minor %d (pid %d)\n", __func__,
        imajor(inode),
        iminor(inode),
        current->pid
        );

    return 0;
}

static int WIFI_close(struct inode *inode, struct file *file)
{
    WIFI_INFO_FUNC("%s: major %d minor %d (pid %d)\n", __func__,
        imajor(inode),
        iminor(inode),
        current->pid
        );
    retflag_wifi = 0;

    return 0;
}

ssize_t WIFI_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    int retval = -EIO;
    char local[12] = {0};
    static int opened = 0;
	int ret = -1;
	struct net_device *netdev = NULL;
	PARAM_CUSTOM_P2P_SET_STRUC_T p2pmode;
	int wait_cnt = 0;
	printk("WIFI_write: count: %d\n", count);
    down(&wr_mtx);
	if (count <= 0) {
		WIFI_INFO_FUNC("WIFI_write invalid param \n");
		goto done;
	}
	if (0 == copy_from_user(local, buf, (count > sizeof(local)) ? sizeof(local) : count)) {
		/*mtk80707 rollback aosp hal*/
		local[11] = 0;
	    WIFI_INFO_FUNC("WIFI_write %s\n", local);

            if (local[0] == '0' && opened == 1) {
                //TODO
                //Configure the EINT pin to GPIO mode.
			p2pmode.u4Enable = 0;
			p2pmode.u4Mode = 0;

			/*IF power off already*/
			if (power_state == 0) {
				WIFI_INFO_FUNC("WMT turn off WIFI OK!\n");
	            opened = 0;
	            retval = count;
				wlan_mode = WLAN_MODE_HALT;
				goto done;
			}
			if (flagIsIFchanged==1)
				{
			     netdev = dev_get_by_name(&init_net, WLAN_LEG_IFACE_NAME);}
		    else
		    	{netdev = dev_get_by_name(&init_net, WLAN_IFACE_NAME);}
			while (netdev == NULL && wait_cnt < 10) {
				WIFI_WARN_FUNC("WMT fail to get wlan0 net device, sleep 300ms\n");
				msleep(300);
				wait_cnt ++;
				if (flagIsIFchanged==1)
				{ netdev = dev_get_by_name(&init_net, WLAN_LEG_IFACE_NAME);}
		        else
		    	{ netdev = dev_get_by_name(&init_net, WLAN_IFACE_NAME);}
			}
			if (wait_cnt >= 10) {
				WIFI_WARN_FUNC("WMT get wlan0 net device time out\n");
				goto done;
			}
			if (pf_set_p2p_mode) {
				ret = pf_set_p2p_mode(netdev, p2pmode);
				if (ret != 0) {
					WIFI_WARN_FUNC("WMT trun off p2p & ap mode ret = %d", ret);
					goto done;
				}
				//msleep(300);
			}
			dev_put(netdev);
			netdev = NULL;

                if (false == mtk_wcn_wifi_func_off()) {
                    WIFI_INFO_FUNC("WMT turn off WIFI fail!\n");
                }
                else {
					msleep(500); //wait for sdio core remove the wifi card, may need use completition to sync the status
                    WIFI_INFO_FUNC("WMT turn off WIFI OK!\n");
                    opened = 0;
                    retval = count;
				wlan_mode = WLAN_MODE_HALT;
				power_state = 0;
				flagIsIFchanged=0;
				PowerOnIFname = 0;
                }
            }
            else if (local[0] == '1') {
                //TODO
                //Disable EINT(external interrupt), and set the GPIO to EINT mode.
                if (power_state == 1){
					WIFI_INFO_FUNC("WIFI is already on!\n");
                    retval = count;
                	}
				else {
			    pf_set_p2p_mode = NULL;
                if (false == mtk_wcn_wifi_func_on()) {
                    WIFI_WARN_FUNC("WMT turn on WIFI fail!\n");
                }
                else {
                    opened = 1;
                    retval = count;
                    WIFI_INFO_FUNC("WMT turn on WIFI success!\n");
				    wlan_mode = WLAN_MODE_HALT;
				    power_state = 1;
					//msleep(300);
                }
				    }
            }
		else if (local[0] == 'S' || local[0] == 'P' || local[0] == 'A') {	
			p2pmode.u4Enable = 1;
			p2pmode.u4Mode = 0;
			ret = -1;

            if (power_state ==0) {
				WIFI_INFO_FUNC("Turn on WIFI first if WIFI is off\n");
				if(local[0] == 'A')
					{
					 PowerOnIFname = 1; //legacy_wlan0
					 printk("change PoweronIFname\n");
					}
				pf_set_p2p_mode = NULL;
                if (false == mtk_wcn_wifi_func_on()) {
                    WIFI_WARN_FUNC("WMT turn on WIFI fail!\n");
                }
                else {
                    opened = 1;
                    retval = count;
                    WIFI_INFO_FUNC("WMT turn on WIFI success!\n");
				    wlan_mode = WLAN_MODE_HALT;
				    power_state = 1;
					if (local[0] == 'A'){
					flagIsIFchanged=1;}
                }
				
			}
			if (flagIsIFchanged==1)
				{netdev = dev_get_by_name(&init_net, WLAN_LEG_IFACE_NAME);
			    }
			else {
			    netdev = dev_get_by_name(&init_net, WLAN_IFACE_NAME);
				if (local[0] == 'A'&&netdev!=NULL)
					{
					 rtnl_lock();
		             ret = dev_change_name(netdev,WLAN_LEG_IFACE_NAME);
		             rtnl_unlock();
					 flagIsIFchanged=1;
					 printk("change_leagcy_name, ret = %d",ret);
					}
			     }
			while (netdev == NULL && wait_cnt < 10) {
				WIFI_WARN_FUNC("WMT fail to get wlan0 net device, sleep 300ms\n");
				msleep(300);
				wait_cnt ++;
				if (flagIsIFchanged==1)
				{netdev = dev_get_by_name(&init_net, WLAN_LEG_IFACE_NAME);}
			    else {
			     netdev = dev_get_by_name(&init_net, WLAN_IFACE_NAME);
                 if (local[0] == 'A'&&netdev!=NULL)
					{
					 rtnl_lock();
		             ret = dev_change_name(netdev,WLAN_LEG_IFACE_NAME);
		             rtnl_unlock();
					 flagIsIFchanged=1;
					 printk("change_leagcy_name, ret = %d",ret);
					}
			}
			    }
			if (wait_cnt >= 10) {
				WIFI_WARN_FUNC("WMT get wlan0 net device time out\n");
				goto done;
        }
			if (pf_set_p2p_mode == NULL) {
				WIFI_INFO_FUNC("set p2p handler is NULL\n");
				goto done;
			}
			if (wlan_mode == WLAN_MODE_AP&&(local[0] == 'S' || local[0] == 'P') )
				{
			     p2pmode.u4Enable = 0;
			     p2pmode.u4Mode = 0;
				 ret = pf_set_p2p_mode(netdev, p2pmode);
				// msleep(300);
				 WIFI_INFO_FUNC("success to turn off p2p/AP mode\n");
				}
			p2pmode.u4Enable = 1;
			p2pmode.u4Mode = 0;
			ret = -1;
			if (local[0] == 'A') {
				p2pmode.u4Mode = 1;
            }
			ret = pf_set_p2p_mode(netdev, p2pmode);
			//msleep(300);
			dev_put(netdev);
			netdev = NULL;
			WIFI_INFO_FUNC("WMT WIFI set p2p mode ret=%d\n", ret);

			if (ret == 0 && (local[0] == 'S' || local[0] == 'P')) {
				WIFI_INFO_FUNC("wlan mode %d --> %d\n", wlan_mode, WLAN_MODE_STA_P2P);
				wlan_mode = WLAN_MODE_STA_P2P;
				retval = count;
			} else if (ret == 0 && local[0] == 'A') {
				WIFI_INFO_FUNC("wlan mode %d --> %d\n", wlan_mode, WLAN_MODE_AP);
				wlan_mode = WLAN_MODE_AP;
				retval = count;
			} else {
				WIFI_INFO_FUNC("fail to set wlan mode\n");
			}
		}
	}
done:
    up(&wr_mtx);
	if (netdev != NULL) {
		dev_put(netdev);
	}
	printk("WIFI_write: retval: %d\n", retval);
	if(retval!=count)
		printk("WIFI_write error\n");
    return (retval);
}


struct file_operations WIFI_fops = {
    .open = WIFI_open,
    .release = WIFI_close,
    .write = WIFI_write,
};
#if REMOVE_MK_NODE
	struct class * wmtWifi_class = NULL;
#endif

static int __init WIFI_init(void)
{
    dev_t dev = MKDEV(WIFI_major, 0);
    int alloc_ret = 0;
    int cdev_err = 0;
#if REMOVE_MK_NODE
		struct device * wmtWifi_dev = NULL;
#endif

    /*static allocate chrdev*/
    alloc_ret = register_chrdev_region(dev, 1, WIFI_DRIVER_NAME);
    if (alloc_ret) {
        WIFI_ERR_FUNC("fail to register chrdev\n");
        return alloc_ret;
    }

    cdev_init(&WIFI_cdev, &WIFI_fops);
    WIFI_cdev.owner = THIS_MODULE;

    cdev_err = cdev_add(&WIFI_cdev, dev, WIFI_devs);
    if (cdev_err) {
        goto error;
    }
#if REMOVE_MK_NODE  //mknod replace
		
	wmtWifi_class = class_create(THIS_MODULE,"wmtWifi");
	if(IS_ERR(wmtWifi_class))
		goto error;
	wmtWifi_dev = device_create(wmtWifi_class,NULL,dev,NULL,"wmtWifi");
	if(IS_ERR(wmtWifi_dev))
		goto error;
#endif

    sema_init(&wr_mtx, 1);

    WIFI_INFO_FUNC("%s driver(major %d) installed.\n", WIFI_DRIVER_NAME, WIFI_major);
    retflag_wifi = 0;
    wlan_mode = WLAN_MODE_HALT;
    pf_set_p2p_mode = NULL;
    return 0;

error:
#if REMOVE_MK_NODE
	if(!IS_ERR(wmtWifi_dev))
		device_destroy(wmtWifi_class,dev);
	if(!IS_ERR(wmtWifi_class)){
		class_destroy(wmtWifi_class);
		wmtWifi_class = NULL;
	}
#endif
    if (cdev_err == 0) {
        cdev_del(&WIFI_cdev);
    }

    if (alloc_ret == 0) {
        unregister_chrdev_region(dev, WIFI_devs);
    }

    return -1;
}

static void __exit WIFI_exit(void)
{
    dev_t dev = MKDEV(WIFI_major, 0);
    retflag_wifi = 0;

#if REMOVE_MK_NODE
		device_destroy(wmtWifi_class,dev);
		class_destroy(wmtWifi_class);
		wmtWifi_class = NULL;
#endif

    cdev_del(&WIFI_cdev);
    unregister_chrdev_region(dev, WIFI_devs);

    WIFI_INFO_FUNC("%s driver removed.\n", WIFI_DRIVER_NAME);
}
EXPORT_SYMBOL(register_set_p2p_mode_handler);
EXPORT_SYMBOL(mt5931_power_off);

module_init(WIFI_init);
module_exit(WIFI_exit);


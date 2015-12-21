#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/of_gpio.h>

#include "linux_wlan_spi.h"
#include "wilc_wfi_netdevice.h"
#include "linux_wlan_common.h"
#include "wilc_wlan_if.h"

#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <asm/uaccess.h>

#define AMAUDIO_MODULE_NAME "amaudio"
#define AMAUDIO_DRIVER_NAME "amaudio"
#define AMAUDIO_DEVICE_NAME "amaudio"
#define AMAUDIO_CLASS_NAME  "amaudio"



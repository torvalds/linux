#ifndef __PLAT_BOARD_H
#define __PLAT_BOARD_H
#include <linux/types.h>
#include <linux/init.h>

#define BOOT_MODE_NORMAL		0
#define BOOT_MODE_FACTORY2		1
#define BOOT_MODE_RECOVERY		2
#define BOOT_MODE_CHARGE		3
#define BOOT_MODE_POWER_TEST		4
#define BOOT_MODE_OFFMODE_CHARGING	5
#define BOOT_MODE_REBOOT		6
#define BOOT_MODE_PANIC			7

struct rk30_i2c_platform_data {
    char *name;
    int  bus_num; 
#define I2C_RK29_ADAP   0
#define I2C_RK30_ADAP   1
    int adap_type:1;
    int is_div_from_arm:1;
    u32  flags;
    int (*io_init)(void);
    int (*io_deinit)(void);
};

int board_boot_mode(void);

/* for USB detection */
#ifdef CONFIG_USB_GADGET
int board_usb_detect_init(unsigned gpio);
#else
static int inline board_usb_detect_init(unsigned gpio) { return 0; }
#endif

/* for wakeup Android */
void rk28_send_wakeup_key(void);

/* for reserved memory 
 * function: board_mem_reserve_add 
 * return value: start address of reserved memory */
phys_addr_t __init board_mem_reserve_add(char *name, size_t size);
void __init board_mem_reserved(void);
#endif

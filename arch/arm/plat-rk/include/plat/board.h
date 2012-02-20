#ifndef __PLAT_BOARD_H
#define __PLAT_BOARD_H
#include <linux/types.h>
#include <linux/init.h>

/*spi*/
struct spi_cs_gpio {
	const char *name;
	unsigned int cs_gpio;
	char *cs_iomux_name;
	unsigned int cs_iomux_mode;
};

struct rk29xx_spi_platform_data {
	int (*io_init)(struct spi_cs_gpio*, int);
	int (*io_deinit)(struct spi_cs_gpio*, int);
	int (*io_fix_leakage_bug)(void);
	int (*io_resume_leakage_bug)(void);
	struct spi_cs_gpio *chipselect_gpios;
	u16 num_chipselect;
};

struct rk29_bl_info {
	u32 pwm_id;
	u32 bl_ref;
	int (*io_init)(void);
	int (*io_deinit)(void);
	int (*pwm_suspend)(void);
	int (*pwm_resume)(void);
	int min_brightness;	/* 0 ~ 255 */
	unsigned int delay_ms;	/* in milliseconds */
};

#define BOOT_MODE_NORMAL		0
#define BOOT_MODE_FACTORY2		1
#define BOOT_MODE_RECOVERY		2
#define BOOT_MODE_CHARGE		3
#define BOOT_MODE_POWER_TEST		4
#define BOOT_MODE_OFFMODE_CHARGING	5
#define BOOT_MODE_REBOOT		6
#define BOOT_MODE_PANIC			7

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

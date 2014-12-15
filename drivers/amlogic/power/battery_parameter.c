
#include <linux/amlogic/battery_parameter.h>
#include <asm/setup.h>
#include <asm/string.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/printk.h>
#include <linux/module.h>

/*
 * this file is use to copy battery parameters passed from uboot to kernel
 * struct, parameters are global data and can be used by different types of 
 * PMU driver.
 */

static struct battery_parameter board_battery_para_from_uboot = {};
static int    battery_paramter_uboot = UBOOT_BATTERY_PARA_FAILED;

static int __init parse_tag_battery(const struct tag *tag)
{
    /*
     * Note: here we only copy data from uboot, checking each data if they are valid
     * should be done by your PMU driver.
     */
    memcpy(&board_battery_para_from_uboot, 
           &tag->u.board_battery.battery_para, 
           sizeof(struct battery_parameter));
    if (board_battery_para_from_uboot.pmu_battery_cap == 0 || 
        board_battery_para_from_uboot.pmu_battery_rdc == 0 ||
        board_battery_para_from_uboot.pmu_charge_efficiency == 0) {
        printk("Wrong parameters from uboot, we use defaut configs\n");    
        return 0;
    }
    battery_paramter_uboot = UBOOT_BATTERY_PARA_SUCCESS;
    printk("Battery parameters got from uboot\n");

    return 0;
}
__tagtable(ATAG_BATTERY, parse_tag_battery);

/*
 * tell driver if we have got battery parameters from uboot 
 */
int get_uboot_battery_para_status(void)
{
    return battery_paramter_uboot; 
}
EXPORT_SYMBOL_GPL(get_uboot_battery_para_status);

/*
 * export battery parameters we saved to driver
 */
struct battery_parameter *get_uboot_battery_para(void)
{
    return &board_battery_para_from_uboot;    
}
EXPORT_SYMBOL_GPL(get_uboot_battery_para);


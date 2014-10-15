
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/uts.h>
#include <linux/utsname.h>
#include <generated/utsrelease.h>
#include <linux/version.h>

#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/slab.h>

#include <linux/debugfs.h>
#include <linux/amlogic/aml_pmu_common.h>
#include <linux/amlogic/aml_rtc.h>
#include <linux/amlogic/battery_parameter.h>

extern struct aml_pmu_api aml_pmu_common_api;

static int aml_pmu_algorithm_init(void)
{
    aml_pmu_register_api(&aml_pmu_common_api);
    return 0;    
}

static void aml_pmu_algorithm_exit(void)
{
    aml_pmu_clear_api();    
}

module_init(aml_pmu_algorithm_init);
module_exit(aml_pmu_algorithm_exit);

MODULE_DESCRIPTION("Amlogic PMU algorithm modules");
MODULE_AUTHOR("tao.zeng@amlogic.com");
MODULE_LICENSE("Proprietary");

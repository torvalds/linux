#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <aw_pm.h>
#include <sys/stat.h>


#define SYS_PM_STAT_PATH	"/sys/power/state"
#define PMU_DEV_PATH		"/dev/pm"



int main(void)
{
	int pm_dev, pm_state, bin_file, ret, i;
	char test_buf[128];
	struct stat fbuf;
	char *func_buf;
    struct aw_pm_info  pm_info;

    printf("---------------------------------------------------\n");
    printf(" start to test pm:\n");
    printf("---------------------------------------------------\n");

    /* config pmu */
	pm_dev = open(PMU_DEV_PATH, O_RDWR);
	if(pm_dev==-1){
		printf("cant open pmu device!\n");
		exit(-19);
	}
    /* set standby information to pm device */
    pm_info.standby_para.event =  SUSPEND_WAKEUP_SRC_USB    \
                                | SUSPEND_WAKEUP_SRC_KEY    \
                                | SUSPEND_WAKEUP_SRC_IR     \
                                | SUSPEND_WAKEUP_SRC_TIMEOFF;
    pm_info.standby_para.time_off = 120;

    pm_info.pmu_arg.twi_port = 0;
    pm_info.pmu_arg.dev_addr = 0x28;
	ioctl(pm_dev, AW_PMU_SET, &pm_info);
	close(pm_dev);

	/*****************************************
	enter standby
	*******************************************/
	pm_state = open(SYS_PM_STAT_PATH, O_RDWR);
	if(pm_state==-1){
		printf("cant open power state\n");
		exit(-19);
	}
	/*read status*/
	ret = read(pm_state, test_buf, 128);
	if(ret>0){
		test_buf[ret] = '\0';
		printf("power state:%s\n", test_buf);
		memcpy(test_buf, "mem", 3);
		write(pm_state, test_buf, 3);
	}else{
		printf("read failed :%d\n", ret);
	}
	close(pm_state);

    printf("---------------------------------------------------\n");
    printf(" test pm end!\n");
    printf("---------------------------------------------------\n");

	exit(0);
}


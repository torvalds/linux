/*
 * arch/arm/mach-sun3i/pm/test/pm_test.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#include <sun3i_pm.h>

#define TEST_SYS_STANDBY	1
#define TEST_SYS_POWER	2
#define TEST_SYS_DPOWER	3
#define TEST_PM_NL			4

#define TEST_POWER_MODE	TEST_SYS_STANDBY

#if TEST_POWER_MODE==TEST_SYS_STANDBY
#define TEST_WAKE_MODE	1

#define STANDBY_BIN_PATH	"/bin/standby.bin"
#define SYS_PM_STAT_PATH	"/sys/power/state"
#elif TEST_POWER_MODE==TEST_SYS_POWER
#define TEST_POFF_MODE		PM_SPEC_MODE

void rtc_print(rtc_date_t *rtc_date,rtc_time_t *rtc_time)
{
	printf("\n\nget_rtc:%d:%d:%d:%d\n",rtc_date->year,rtc_date->month,rtc_date->day,rtc_date->wday);
	printf("%d:%d:%d\n",rtc_time->hour,rtc_time->min,rtc_time->sec);
}
#elif TEST_POWER_MODE==TEST_PM_NL
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <arpa/inet.h>

#define PM_NETLINK_TEST		31

static int sock_fd = -1;

struct nl_msg
{
	struct nlmsghdr hdr;
	__u32 data[4];
};
#endif

#define TEST_DEFAULT_MIN	10
#define TEST_DEFAULT_SEC	30

#define PMU_DEV_PATH		"/dev/pm"

int main(void)
{
	int fd,ret,i;
	char test_buf[128];
	char *func_buf;
	struct stat fbuf;
	struct aw_pm_info pm_info;
	struct aw_pm_arg pm_arg;

	printf("This test code is mainly for pm\n");

#if TEST_POWER_MODE==TEST_SYS_STANDBY
	/*****************************************
	config standby info
	******************************************/
	fd = open(PMU_DEV_PATH,O_RDWR);
	if(fd==-1){
		printf("can not open pm device\n");
		exit(-19);
	}
	ioctl(fd,AW_PMU_VALID,&ret);
	if(!ret){
		/****************************************
		read standby.bin
		******************************************/
		int tfd;
		ret = stat(STANDBY_BIN_PATH,&fbuf);
		if(ret<0)
			exit(-19);
		func_buf = (char *)malloc(fbuf.st_size);
		if(!func_buf)
			exit (-12);

		tfd = open(STANDBY_BIN_PATH,O_RDONLY);
		if(tfd==-1){
			printf("open standby.bin fail\n");
			exit(-19);
		}
		ret = read(tfd,func_buf,fbuf.st_size);
		close(tfd);

		/********set pmu**********/
		memset(&pm_info,0,sizeof(struct aw_pm_info));
		pm_info.func_addr = func_buf;
		pm_info.func_size = fbuf.st_size;
		pm_info.arg.wakeup_mode = TEST_WAKE_MODE;
	#if 0
		switch(pm_info.arg.wakeup_mode){
		case RTC_DC_MODE:{
			rtc_date_t rtc_date;
			rtc_time_t rtc_time;

			rtc_date.year = 2010;
			rtc_date.month = 4;
			rtc_date.day = 23;
			rtc_date.wday = 5;

			rtc_time.hour = 10;
			rtc_time.min = TEST_DEFAULT_MIN;
			rtc_time.sec =TEST_DEFAULT_SEC;
			tm_set_rtc(&rtc_date,&rtc_time); //set rtc time
			sleep(1);
			tm_get_rtc(&rtc_date,&rtc_time);
			printf("time %d:%d:%d\n",rtc_time.hour,rtc_time.min,rtc_time.sec);

			rtc_time.sec += 10;
			pm_info.arg.param[0] = AM_PMU_MK_DATE(rtc_date.year, rtc_date.month, rtc_date.day);	//alarm date
			pm_info.arg.param[1] = AM_PMU_MK_TIME(rtc_time.hour,rtc_time.min,rtc_time.sec);	// alarm time
			break;
		}
		case EXT_DC_MODE:
			printf("Hey guys, this mode has not been implemented yet!");
			break;
		case KEYE_DC_MODE:
			pm_info.arg.param[0] = 3;	//count times
			pm_info.arg.param[1] = 6;	//key number
			break;
		case IRE_DC_MODE:
			pm_info.arg.param[0] = 1;   //valid code number
			/*irkey code*/
			pm_info.arg.param[1] = 0x12;//0xc0;//0x12;
			pm_info.arg.param[2] = 0;
			pm_info.arg.param[3] = 0;
			pm_info.arg.param[4] = 0;
			break;
		case KEY_NDC_MODE:
			pm_info.arg.param[0] = 60;	//gpio num
			break;
		default:
			break;
		}
	#endif
		ioctl(fd,AW_PMU_SET,&pm_info);
	}
	close(fd);
	/*****************************************
	enter standby
	*******************************************/
	fd = open(SYS_PM_STAT_PATH, O_RDWR);
	if(fd==-1){
		printf("cant open power state\n");
		exit(-19);
	}

	/*read status*/
	ret = read(fd,test_buf,128);
	if(ret>0){
		test_buf[ret] = '\0';
		printf("power state:%s\n",test_buf);
		memcpy(test_buf,"mem",3);
		write(fd,test_buf,3);
	}else{
		printf("read failed :%d\n",ret);
	}

	close(fd);
#elif TEST_POWER_MODE==TEST_SYS_POWER
	rtc_date_t rtc_date;
	rtc_time_t rtc_time;
	fd = open(PMU_DEV_PATH,O_RDWR);
	if(fd==-1){
		printf("can not open pmu device\n");
		exit(-19);
	}
	pm_arg.wakeup_mode = TEST_POFF_MODE;

	rtc_date.year = 2010;
	rtc_date.month = 4;
	rtc_date.day = 23;
	rtc_date.wday = 5;

	rtc_time.hour = 10;
	rtc_time.min = TEST_DEFAULT_MIN;
	rtc_time.sec =TEST_DEFAULT_SEC;
	tm_set_rtc(&rtc_date,&rtc_time); //set rtc time
	sleep(1);
	tm_get_rtc(&rtc_date,&rtc_time);
	printf("time %d:%d:%d\n",rtc_time.hour,rtc_time.min,rtc_time.sec);

	rtc_time.sec += 10;
	pm_arg.param[0] = AM_PMU_MK_DATE(rtc_date.year, rtc_date.month, rtc_date.day);	//on alarm date
	pm_arg.param[1] = AM_PMU_MK_TIME(rtc_time.hour,rtc_time.min,rtc_time.sec);	// on alarm time

	if(pm_arg.wakeup_mode == PM_GEN_MODE)
		pm_arg.param[2] = RTC_ALARM_GPO_LOW;
	else
		pm_arg.param[2] = RTC_ALARM_PW_MAX-4;
	rtc_time.sec += 10;
	pm_arg.param[3] = AM_PMU_MK_DATE(rtc_date.year, rtc_date.month, rtc_date.day);	//off alarm date
	pm_arg.param[4] = AM_PMU_MK_TIME(rtc_time.hour,rtc_time.min,rtc_time.sec);	// off alarm time

	ioctl(fd,AM_PMU_POFF,&pm_arg);

	close(fd);

	while(1){
		sleep(1);
		tm_get_rtc(&rtc_date,&rtc_time);
		rtc_print(&rtc_date,&rtc_time);
		if(rtc_time.min-TEST_DEFAULT_MIN>0)
			break;
	}
#elif TEST_POWER_MODE==TEST_SYS_DPOWER
	fd = open(PMU_DEV_PATH,O_RDWR);
	if(fd==-1){
		printf("can not open pmu device\n");
		exit(-19);
	}
	for(i=0;i<0xffff;i++){
		printf("enter low\n");
		ioctl(fd,AM_PMU_PLOW,NULL);
		sleep(1);
		printf("enter high\n");
		ioctl(fd,AM_PMU_PHIGH,NULL);
		sleep(1);
	}
	close(fd);
#elif TEST_POWER_MODE==TEST_PM_NL
	struct sockaddr_nl local; //local {user space}
    	struct sockaddr_nl kpeer; //peer {kernel space}
    	struct nl_msg message,kmsg;
    	int k_peer_len,rcv_len;
    	struct in_addr addr;

	sock_fd = socket(AF_NETLINK,SOCK_RAW,PM_NETLINK_TEST);
	if(sock_fd<0){
		perror("create socket error!\n");
		exit(-errno);
	}

	memset(&local,0,sizeof(local));
	local.nl_family = AF_NETLINK;
	local.nl_pid = getpid();
	local.nl_groups = 0;
	if(bind(sock_fd,(struct sockaddr *)&local,sizeof(local)) != 0){
        	perror("bind error\n");
        	exit(-1);
   	}

    	memset(&kpeer,0,sizeof(kpeer));
	kpeer.nl_family = AF_NETLINK;
	kpeer.nl_pid = 0;
	kpeer.nl_groups = 0; //not in multicast

 	memset(&message,0,sizeof(message));
	message.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(message.data));
	message.hdr.nlmsg_flags = 0;
	message.hdr.nlmsg_type = 0xAB;
	message.hdr.nlmsg_pid = local.nl_pid;
	message.data[0]=0xdd;

	//send to kernel
	printf("%d send msg to kernel\n",local.nl_pid);
    	sendto(sock_fd,&message,message.hdr.nlmsg_len,0,(struct sockaddr *)&kpeer,sizeof(kpeer));

	while(1){
		k_peer_len = sizeof(struct sockaddr_nl);
		rcv_len = recvfrom(sock_fd,&kmsg,sizeof(kmsg),0,(struct sockaddr *)&kpeer,(socklen_t *)&k_peer_len);
		printf("rec msg from kernel:%d,ret=%d\n",kmsg.data[0],rcv_len);
		break;
	}

#endif

	exit(0);
}


/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        AllWinner Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : common.h
* By      : kevin.z
* Version : v1.0
* Date    : 2011-5-30 17:21
* Descript: common lib for standby.
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/

#include <linux/types.h>

#ifndef __COMMON_H__
#define __COMMON_H__

static inline __u64 standby_uldiv(__u64 dividend, __u32 divisior)
{
    __u64   tmpDiv = (__u64)divisior;
    __u64   tmpQuot = 0;
    __s32   shift = 0;

    if(!divisior)
    {
        /* divide 0 error abort */
        return 0;
    }

    while(!(tmpDiv & ((__u64)1<<63)))
    {
        tmpDiv <<= 1;
        shift ++;
    }

    do
    {
        if(dividend >= tmpDiv)
        {
            dividend -= tmpDiv;
            tmpQuot = (tmpQuot << 1) | 1;
        }
        else
        {
            tmpQuot = (tmpQuot << 1) | 0;
        }
        tmpDiv >>= 1;
        shift --;
    } while(shift >= 0);

    return tmpQuot;
}



void standby_memcpy(void *dest, void *src, int n);
void standby_mdelay(int ms);
void standby_delay(int cycle);

#endif  //__COMMON_H__


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
* Descript: common lib for mem.
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/
#ifndef __COMMON_H__
#define __COMMON_H__

static inline __u64 mem_uldiv(__u64 dividend, __u32 divisior)
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



void mem_memcpy(void *dest, void *src, int n);

/*notice: all the delay cycle is measured by 60M hz 
 *when in super mem, the os is running in 1008M
 *so, the delay cycle need reconsideration.
 */
void mem_delay(int cycle);

#endif  //__COMMON_H__


/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        AllWinner Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : common.c
* By      : kevin.z
* Version : v1.0
* Date    : 2011-5-30 19:38
* Descript: common lib for mem
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/
#include "super_i.h"


/*
*********************************************************************************************************
*                           mem_memcpy
*
*Description: memory copy function for mem.
*
*Arguments  :
*
*Return     :
*
*Notes      :
*
*********************************************************************************************************
*/
void mem_memcpy(void *dest, void *src, int n)
{
    char    *tmp_src = (char *)src;
    char    *tmp_dst = (char *)dest;

  //  if(!dest || !src){
        /* parameter is invalid */
  //      return;
 //   }

    for( ; n > 0; n--){
        *tmp_dst ++ = *tmp_src ++;
    }

    return;
}




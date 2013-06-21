/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        AllWinner Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2012-2015,  China
*                                             All Rights Reserved
*
* File    : mem_printk.h
* By      : young
* Version : v1.0
* Date    : 2011-5-30 19:50
* Descript: intterupt bsp for platform standby.
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/
#ifndef __MEM_PRINTK_H__
#define __MEM_PRINTK_H__

#define DEBUG_BUFFER_SIZE (256)
__s32 printk(const char *format, ...);
__s32 printk_nommu(const char *format, ...);

#define  print_call_info_nommu(...)({										\
		do{															\
			printk_nommu("%s, %s, %d. \n" , __FILE__, __func__, __LINE__);\
		}while(0);})

#define  print_call_info(...)({										\
		do{															\
			printk("%s, %s, %d. \n" , __FILE__, __func__, __LINE__);\
		}while(0);})

#endif  //__MEM_PRINTK_H__